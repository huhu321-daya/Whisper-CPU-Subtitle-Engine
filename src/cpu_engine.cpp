/*
 * Whisper CPU Subtitle Engine
 * 纯 CPU 实时语音转字幕引擎，静态链接 whisper.cpp，无外部 DLL 依赖。
 *
 * IPC 报文格式（12 字节头 magic/type/len，magic 固定 0x11111111）：
 *   接收 0x10 INIT(UTF-16LE 模型路径) / 0x20 控制(偏移36=播放位置ms) / 0x21 音频(float32 PCM 16kHz)
 *   发送 0x11 ACK(1字节，必须在加载模型前) / 0x10000 文本 / 0x20000 24字节时间戳
 *        0x20001 字幕文本 / 0x22 字幕文本副本
 *
 * 时间戳结构：int64_t ts[3] = { t0, 起始ms, 结束ms }，结束ms 最少为起始ms+200。
 * 0x20000 的 24 字节必须作为 body 发送，不能拆成裸报文。
 */
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>
#include <windows.h>

#include "whisper.h"

extern "C" struct ggml_backend_reg * ggml_backend_cpu_reg(void);

#define WHISPER_SAMPLE_RATE 16000

struct IpcMsg {
    unsigned int magic;
    unsigned int type;
    unsigned int len;
};

static HANDLE g_pipe = INVALID_HANDLE_VALUE;
static FILE *g_log = nullptr;
static long long g_baseMs = 0;
static long long g_audioSinceBase = 0;

static void log_body(const char *tag, const void *data, unsigned int len);

static bool ipc_send(unsigned int type, const void *data, unsigned int len) {
    unsigned char buf[12 + 8192];
    if (len > sizeof(buf) - 12) return false;
    IpcMsg hdr{0x11111111, type, len};
    memcpy(buf, &hdr, 12);
    if (len > 0 && data) memcpy(buf + 12, data, len);
    DWORD w;
    /* 头与体合并为一次 WriteFile，保证在消息管道中是一条完整消息 */
    if (!WriteFile(g_pipe, buf, 12 + len, &w, NULL) || w != 12 + len) return false;
    if (g_log) { fprintf(g_log, "SEND magic=1 type=0x%x len=%u\n", type, len); }
    log_body("m1", data, len);
    return true;
}

static void log_body(const char *tag, const void *data, unsigned int len) {
    if (!g_log || !data || len == 0) return;
    const unsigned char *p = (const unsigned char *)data;
    unsigned int show = (strcmp(tag, "cfg") == 0) ? len : (len < 32 ? len : 32);
    if (show > 128) show = 128;
    fprintf(g_log, "  BODY %s len=%u hex=", tag, len);
    for (unsigned int i = 0; i < show; i++) fprintf(g_log, "%02x", p[i]);
    fprintf(g_log, " ascii=");
    for (unsigned int i = 0; i < show; i++) {
        unsigned char c = p[i];
        fprintf(g_log, "%c", (c >= 32 && c < 127) ? c : '.');
    }
    fprintf(g_log, "\n");
    fflush(g_log);
}

static bool ipc_recv(IpcMsg &hdr, std::vector<unsigned char> &payload) {
    DWORD r;
    if (!ReadFile(g_pipe, &hdr, 12, &r, NULL) || r != 12) return false;
    if (hdr.magic != 0x11111111) return false;
    payload.resize(hdr.len);
    if (hdr.len > 0) {
        DWORD got = 0;
        while (got < hdr.len) {
            if (!ReadFile(g_pipe, payload.data() + got, hdr.len - got, &r, NULL) || r == 0) return false;
            got += r;
        }
    }
    if (g_log) { fprintf(g_log, "RECV type=0x%x len=%u\n", hdr.type, hdr.len); fflush(g_log); }
    return true;
}

/* whisper/ggml 原始日志 -> 0x10000 LOG 报文原样转发，
   宿主据此显示加载进度并确认引擎就绪 */
static void log_cb(enum ggml_log_level level, const char * text, void * user_data) {
    (void)level; (void)user_data;
    if (!text || !text[0]) return;
    size_t len = strlen(text);
    if (len > 8192) len = 8192;
    char buf[8192 + 1];
    memcpy(buf, text, len);
    buf[len] = 0;
    if (g_log) { fprintf(g_log, "[GGML] %s\n", buf); fflush(g_log); }
    ipc_send(0x10000, buf, (unsigned int)len + 1);
}

static std::string utf16le_to_utf8(const unsigned char *data, unsigned int len) {
    int wlen = len / 2;
    std::vector<wchar_t> wbuf(wlen + 1, 0);
    for (int i = 0; i < wlen; i++) wbuf[i] = data[i * 2] | (data[i * 2 + 1] << 8);
    int u8len = WideCharToMultiByte(CP_UTF8, 0, wbuf.data(), wlen, NULL, 0, NULL, NULL);
    std::string result(u8len, 0);
    WideCharToMultiByte(CP_UTF8, 0, wbuf.data(), wlen, &result[0], u8len, NULL, NULL);
    return result;
}

int main(int argc, char **argv) {
    const char *pipeName = "Whisper20369";
    for (int i = 1; i < argc; i++) {
        if (!strcmp(argv[i], "-IPC") && i + 1 < argc) { pipeName = argv[i + 1]; i++; }
    }

    char tmpDir[MAX_PATH];
    if (GetTempPathA(MAX_PATH, tmpDir)) {
        char logPath[MAX_PATH];
        snprintf(logPath, sizeof(logPath), "%smain64_engine.log", tmpDir);
        g_log = fopen(logPath, "wb");
    }
    if (g_log) { fprintf(g_log, "== engine pipeName=%s\n", pipeName); fflush(g_log); }

    /* 显式取一次 CPU 后端注册表，保证静态链接时该目标文件不被裁掉 */
    ggml_backend_cpu_reg();

    char pn[512];
    snprintf(pn, sizeof(pn), "\\\\.\\pipe\\%s", pipeName);
    g_pipe = CreateFileA(pn, GENERIC_READ | GENERIC_WRITE, 0, NULL, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
    if (g_pipe == INVALID_HANDLE_VALUE) {
        if (g_log) fprintf(g_log, "CONNECT FAIL err=%d\n", GetLastError());
        return 1;
    }
    if (g_log) { fprintf(g_log, "CONNECTED\n"); fflush(g_log); }

    IpcMsg hdr;
    std::vector<unsigned char> payload;

    if (!ipc_recv(hdr, payload)) { if (g_log) fprintf(g_log, "FAIL recv 0x10\n"); return 2; }
    if (hdr.type != 0x10) { if (g_log) fprintf(g_log, "UNEXPECTED type=0x%x\n", hdr.type); return 2; }

    std::string modelPath = utf16le_to_utf8(payload.data(), hdr.len);
    if (g_log) { fprintf(g_log, "MODEL: %s\n", modelPath.c_str()); fflush(g_log); }

    /* ===== ACK 必须在加载前发：播放器收到 ACK 才开始推音频 ===== */
    ipc_send(0x11, "\x00", 1);

    whisper_context_params cparams = whisper_context_default_params();
    cparams.use_gpu = false;
    whisper_log_set(log_cb, nullptr);
    struct whisper_context *ctx = whisper_init_from_file_with_params(modelPath.c_str(), cparams);
    if (!ctx) {
        if (g_log) fprintf(g_log, "FAIL load model\n");
        return 3;
    }
    if (g_log) { fprintf(g_log, "MODEL LOADED OK\n"); fflush(g_log); }

    std::string language = "en";
    std::vector<float> audioBuf;
    audioBuf.reserve(WHISPER_SAMPLE_RATE * 30);

    while (true) {
        if (!ipc_recv(hdr, payload)) {
            if (g_log) fprintf(g_log, "PIPE CLOSED\n");
            break;
        }

        if (hdr.type == 0x20) {
            if (hdr.len >= 2) {
                language.clear();
                for (unsigned int i = 0; i < hdr.len && i < 32; i++) {
                    if (payload[i] == 0) break;
                    language += (char)payload[i];
                }
            }
            if (hdr.len >= 40) {
                unsigned int baseMs = 0;
                memcpy(&baseMs, payload.data() + 36, 4);
                g_baseMs = (long long)baseMs;
                g_audioSinceBase = 0;
            }
            if (g_log) {
                fprintf(g_log, "RECV 0x20 CONFIG len=%u baseMs=%lld\n", hdr.len, g_baseMs);
                log_body("cfg", payload.data(), hdr.len);
            }
            if (g_log) { fprintf(g_log, "CONFIG lang=%s\n", language.c_str()); fflush(g_log); }
        }
        else if (hdr.type == 0x21) {
            unsigned int nFloat = hdr.len / 4;
            if (nFloat == 0) continue;

            const float *pcm = reinterpret_cast<const float*>(payload.data());
            audioBuf.insert(audioBuf.end(), pcm, pcm + nFloat);
            g_audioSinceBase += nFloat;

            if (audioBuf.size() >= (unsigned int)(WHISPER_SAMPLE_RATE * 3)) {
                if (g_log) { fprintf(g_log, "TRANSCRIBE %zu samples (%.1fs)\n", audioBuf.size(), (double)audioBuf.size()/WHISPER_SAMPLE_RATE); fflush(g_log); }

                whisper_full_params wparams = whisper_full_default_params(WHISPER_SAMPLING_GREEDY);
                wparams.language = language.c_str();
                wparams.print_progress = false;
                wparams.print_timestamps = false;
                wparams.print_special = false;
                wparams.single_segment = false;
                wparams.max_tokens = 32;
                wparams.audio_ctx = 768;
                wparams.no_context = true;

                int ret = whisper_full(ctx, wparams, audioBuf.data(), audioBuf.size());
                if (ret == 0) {
                    int nSeg = whisper_full_n_segments(ctx);
                    for (int i = 0; i < nSeg; i++) {
                        const char *text = whisper_full_get_segment_text(ctx, i);
                        if (!text || !text[0]) continue;

                        int64_t t0 = whisper_full_get_segment_t0(ctx, i);
                        int64_t t1 = whisper_full_get_segment_t1(ctx, i);
                        long long liveMs = g_baseMs + (g_audioSinceBase * 1000LL / WHISPER_SAMPLE_RATE) - (long long)(audioBuf.size() * 1000LL / WHISPER_SAMPLE_RATE);
                        long long segBase = liveMs;
                        long long ms0 = (long long)(t0 * 10) + segBase;
                        long long ms1 = (long long)(t1 * 10) + segBase;

                        // 三段式报文：时间戳(24字节) + 字幕文本 + 文本副本
                        {
                            long long ts[3];
                            ts[0] = t0;
                            ts[1] = ms0;
                            ts[2] = (ms1 > ms0 + 200) ? ms1 : (ms0 + 200);
                            ipc_send(0x20000, ts, sizeof(ts));
                            ipc_send(0x20001, text, (unsigned int)strlen(text) + 1);
                            ipc_send(0x22, text, (unsigned int)strlen(text) + 1);

                            char srt[8192];
                            snprintf(srt, sizeof(srt), "[%02lld:%02lld:%02lld.%03lld --> %02lld:%02lld:%02lld.%03lld]%s",
                                     ts[1]/3600000, (ts[1]/60000)%60, (ts[1]/1000)%60, ts[1]%1000,
                                     ts[2]/3600000, (ts[2]/60000)%60, (ts[2]/1000)%60, ts[2]%1000, text);
                            ipc_send(0x10000, srt, (unsigned int)strlen(srt) + 1);
                        }

                        if (g_log) { fprintf(g_log, "SUB [%d/%d] %lld-%lldms: %s\n", i, nSeg, ms0, ms1, text); fflush(g_log); }
                    }
                } else {
                    if (g_log) { fprintf(g_log, "whisper_full FAIL ret=%d\n", ret); fflush(g_log); }
                }
                audioBuf.clear();
            }
        }
    }

    whisper_free(ctx);
    if (g_pipe != INVALID_HANDLE_VALUE) CloseHandle(g_pipe);
    if (g_log) fclose(g_log);
    return 0;
}