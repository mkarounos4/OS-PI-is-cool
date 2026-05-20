
long write_console(const char *s, uint64_t len);
long putc(char c);
long get_ticks(void);
long delay(uint64_t ms);
long exit(int code);
long getpid(void);
long spawn(void);
long waitpid(pid_t pid, int *status, uint32_t flags);
long sbrk(int64_t increment);