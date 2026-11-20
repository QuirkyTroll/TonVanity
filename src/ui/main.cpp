#include "tonvanity.h"
#include <windows.h>
#include <conio.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <string>
#include <vector>
#include <chrono>
#include <thread>

static const char* VER[3] = {"V4R2", "W5R1", "TGWallet"};
static const char* fmt_name(int b) { return b ? "Bounceable (EQ)" : "Non-bounceable (UQ)"; }
static const char* mode_name(int m) { return m == 0 ? "CPU only" : m == 1 ? "CPU+GPU" : "GPU only"; }

static void set_cursor(int x, int y) { COORD c{(SHORT)x,(SHORT)y}; SetConsoleCursorPosition(GetStdHandle(STD_OUTPUT_HANDLE), c); }
static void clear() { system("cls"); }

static std::string input_line(const char* prompt) {
    printf("%s", prompt); fflush(stdout);
    std::string s; int c;
    while ((c = _getch()) != '\r') {
        if (c == 8) { if (!s.empty()) { s.pop_back(); printf("\b \b"); } }
        else if (c >= 32 && c < 127) { s.push_back((char)c); printf("%c", c); }
    }
    printf("\n"); return s;
}

int main(int argc, char** argv) {
    SetConsoleOutputCP(65001);
    if (getenv("TNV_OCL_DEBUG")) fprintf(stderr, "[ui] main start %llu\n", (unsigned long long)GetTickCount64());
    if (argc > 1 && strcmp(argv[1], "--selftest") == 0) {
        char rep[4096]; int r = tnv_selftest(rep, sizeof(rep)); printf("%s", rep); return r;
    }
    if (argc >= 8 && strcmp(argv[1], "--run") == 0) {
        TnvConfig rc; memset(&rc, 0, sizeof(rc));
        rc.wallet_version = atoi(argv[2]);
        rc.mnemonic_scheme = atoi(argv[3]);
        rc.bounceable = atoi(argv[4]);
        rc.compute_mode = atoi(argv[5]);
        rc.case_sensitive = 1; rc.cpu_threads = 0;
        snprintf(rc.suffix, sizeof(rc.suffix), "%s", argv[6]);
        double secs = atof(argv[7]);
        void* h = tnv_create(&rc);
        if (!h) { printf("create failed\n"); return 1; }
        tnv_start(h);
        auto t0 = std::chrono::steady_clock::now();
        while (std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count() < secs) {
            TnvStats st; TnvMatch mm[32];
            int n = tnv_poll(h, &st, mm, 32);
            for (int i = 0; i < n; ++i) printf("FOUND %s | %s\n", mm[i].address, mm[i].mnemonic);
            std::this_thread::sleep_for(std::chrono::milliseconds(200));
        }
        TnvStats st; TnvMatch mm[32]; tnv_poll(h, &st, mm, 0);
        printf("RESULT backend=%s attempts=%llu candidates=%llu found=%llu attempts_per_sec=%.1f candidates_per_sec=%.1f elapsed=%.1f\n",
               st.backend_name, st.attempts, st.candidates, st.found, st.attempts_per_sec, st.candidates_per_sec, st.elapsed_ms / 1000.0);
        fflush(stdout);
        bool ui_dbg = getenv("TNV_OCL_DEBUG") != NULL;
        if (ui_dbg) fprintf(stderr, "[ui] stop start %llu\n", (unsigned long long)GetTickCount64());
        tnv_stop(h);
        if (ui_dbg) fprintf(stderr, "[ui] stop done %llu\n", (unsigned long long)GetTickCount64());
        tnv_destroy(h);
        if (ui_dbg) fprintf(stderr, "[ui] destroy done %llu\n", (unsigned long long)GetTickCount64());
        fflush(stderr);
        TerminateProcess(GetCurrentProcess(), 0);
    }
    TnvConfig c; memset(&c, 0, sizeof(c));
    c.wallet_version = 1; c.mnemonic_scheme = 0; c.bounceable = 0; c.compute_mode = 1;
    c.case_sensitive = 1; c.cpu_threads = 0; strcpy(c.suffix, "AB");
    printf("TonVanity 1.0 — офлайн-генератор TON-адресов\n");
    printf("Доступно: %s\n\n", tnv_backend_info());
    for (;;) {
        clear();
        printf("=== TonVanity ===\n");
        printf("1. Версия кошелька : %s\n", VER[c.wallet_version]);
        printf("2. Мнемоника      : %d слов%s\n", c.mnemonic_scheme ? 24 : 12,
               c.wallet_version == 2 && c.mnemonic_scheme ? " (anchor+signing)" : c.mnemonic_scheme ? " (TON basic)" : " (BIP-39 TEP-3)");
        printf("3. Формат адреса  : %s\n", fmt_name(c.bounceable));
        printf("4. Режим          : %s\n", mode_name(c.compute_mode));
        printf("5. Окончание      : %s\n", c.suffix);
        printf("6. Учитывать регистр: %s\n", c.case_sensitive ? "да" : "нет");
        printf("7. Потоки CPU     : %s\n", c.cpu_threads ? std::to_string(c.cpu_threads).c_str() : "все");
        printf("\n[S] Начать  [1-7] Изменить  [Q] Выход\n");
        printf("Бэкенд: %s\n", tnv_backend_info());
        int k = _getch();
        if (k == 'q' || k == 'Q' || k == 27) return 0;
        if (k >= '1' && k <= '7') {
            int n = k - '0';
            if (n == 1) c.wallet_version = (c.wallet_version + 1) % 3;
            else if (n == 2) c.mnemonic_scheme = c.mnemonic_scheme ? 0 : 1;
            else if (n == 3) c.bounceable = c.bounceable ? 0 : 1;
            else if (n == 4) c.compute_mode = (c.compute_mode + 1) % 3;
            else if (n == 5) {
                std::string s = input_line("Окончание (A-Z a-z 0-9 - _): ");
                if (!s.empty() && s.size() < sizeof(c.suffix)) { memset(c.suffix, 0, sizeof(c.suffix)); strncpy(c.suffix, s.c_str(), sizeof(c.suffix) - 1); }
            } else if (n == 6) c.case_sensitive = c.case_sensitive ? 0 : 1;
            else if (n == 7) {
                std::string s = input_line("Потоков CPU (0 = все): ");
                c.cpu_threads = atoi(s.c_str());
            }
            continue;
        }
        if (k != 's' && k != 'S') continue;
        void* h = tnv_create(&c);
        if (!h) { printf("Ошибка конфигурации (проверьте окончание).\n"); _getch(); continue; }
        printf("Инициализация бэкенда (первый запуск OpenCL может занять 20-40 с)...\n");
        fflush(stdout);
        tnv_start(h);
        clear();
        printf("Запуск: %s %d слов %s | %s | окончание %s\n\n", VER[c.wallet_version], c.mnemonic_scheme ? 24 : 12,
               fmt_name(c.bounceable), mode_name(c.compute_mode), c.suffix);
        auto t0 = std::chrono::steady_clock::now();
        unsigned long long last_found = 0;
        bool running = true;
        while (running) {
            TnvStats s; TnvMatch m[32];
            int n = tnv_poll(h, &s, m, 32);
            for (int i = 0; i < n; ++i) {
                printf("\n*** НАЙДЕНО ***\n%s\n%s\n", m[i].address, m[i].mnemonic);
            }
            if (s.found != last_found) { last_found = s.found; }
            double el = std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count();
            set_cursor(0, 4);
            printf("Бэкенд: %-28s  состояние: %d\n", s.backend_name, s.backend_state);
            printf("Попыток: %-12llu  Кандидатов: %-12llu  Найдено: %llu\n", s.attempts, s.candidates, s.found);
            printf("Скорость: %.1f попыток/с, %.1f кандидатов/с   Время: %.0f с\n", s.attempts_per_sec, s.candidates_per_sec, el);
            printf("Сообщение: %-70s\n", s.message);
            printf("[S] Стоп   [Q] Выход\n");
            if (_kbhit()) {
                int ch = _getch();
                if (ch == 's' || ch == 'S') running = false;
                if (ch == 'q' || ch == 'Q') { running = false; fflush(stdout); tnv_stop(h); tnv_destroy(h); TerminateProcess(GetCurrentProcess(), 0); }
            }
            std::this_thread::sleep_for(std::chrono::milliseconds(200));
        }
        tnv_stop(h); tnv_destroy(h);
        printf("\nОстановлено. Нажмите любую клавишу.\n"); _getch();
    }
}


