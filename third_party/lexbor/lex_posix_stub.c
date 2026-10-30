#include <stddef.h>
#include <stdint.h>

extern void *malloc(size_t size);

void *calloc(size_t nmemb, size_t size) {
    if (nmemb != 0 && size != 0 && nmemb > (size_t)-1 / size) return 0;
    size_t total = nmemb * size;
    void *p = malloc(total);
    if (p) {
        unsigned char *b = (unsigned char *)p;
        for (size_t i = 0; i < total; i++) b[i] = 0;
    }
    return p;
}

float floorf(float x) {
    long i = (long)x;
    if (x < 0.0f && (float)i != x) i--;
    return (float)i;
}

typedef struct lex_stub_FILE lex_stub_FILE;
typedef struct lex_stub_DIR lex_stub_DIR;

lex_stub_FILE *fopen(const char *path, const char *mode) {
    (void)path; (void)mode;
    return 0;
}
int fclose(lex_stub_FILE *f) { (void)f; return -1; }
int feof(lex_stub_FILE *f) { (void)f; return 1; }
size_t fread(void *ptr, size_t size, size_t nmemb, lex_stub_FILE *f) {
    (void)ptr; (void)size; (void)nmemb; (void)f;
    return 0;
}
int fseek(lex_stub_FILE *f, long off, int whence) {
    (void)f; (void)off; (void)whence;
    return -1;
}
long ftell(lex_stub_FILE *f) { (void)f; return -1; }

lex_stub_DIR *opendir(const char *path) { (void)path; return 0; }
lex_stub_DIR *readdir(lex_stub_DIR *d) { (void)d; return 0; }
int closedir(lex_stub_DIR *d) { (void)d; return -1; }

int stat(const char *path, void *buf) { (void)path; (void)buf; return -1; }
