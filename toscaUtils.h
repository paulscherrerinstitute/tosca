#ifndef toscaUtils_h
#define toscaUtils_h

#ifdef __cplusplus
extern "C" {
#endif

size_t strToSize(const char* str);
char* sizeToStr(size_t size, char* str);
#define SIZE_STRING_BUFFER_SIZE 33

#ifdef __cplusplus
}
#endif
#endif
