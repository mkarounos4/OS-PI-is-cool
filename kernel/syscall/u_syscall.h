
long s_write_console(const char *s, uint64_t len);
long s_putc(char c);
long s_get_ticks(void);
long s_delay(uint64_t ms);
long s_exit(int code);
long s_getpid(void);
long s_spawn(void);
long s_waitpid(pid_t pid, int *status, uint32_t flags);
long s_sbrk(int64_t increment);