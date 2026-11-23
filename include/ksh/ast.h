#ifndef KSH_AST_H
#define KSH_AST_H

#include <ksh/common.h>

typedef enum FdRedirectKind {
    FD_REDIRECT_NONE,
    FD_REDIRECT_READ,
    FD_REDIRECT_WRITE,
    FD_REDIRECT_APPEND,
    FD_REDIRECT_DUP,
    FD_REDIRECT_CLOSE
} FdRedirectKind;

/* Redirection of descriptor N (0-9) written as N<file, N>file, N>>file, N<&M, N>&M or N>&-.
   Descriptors 0-2 with plain file targets still use the in/out/err fields of CommandAST. */
typedef struct FdRedirect {
    FdRedirectKind kind;
    wchar_t* path;
    int dupFrom;
} FdRedirect;

#define KSH_FD_COUNT 10

typedef struct CommandAST {
    int argc;
    wchar_t* argv[MAX_ARGS];
    wchar_t* in_redirect;
    wchar_t* out_redirect;
    wchar_t* err_redirect;
    bool append_out;
    bool append_err;
    bool err_to_out;
    bool out_to_err;
    bool err_to_out_before_out;
    bool out_to_err_before_err;
    bool is_temp_in_redirect;
    FdRedirect fds[KSH_FD_COUNT];
    struct CommandAST* next_in_pipeline;
    KshArena* arena;
} CommandAST;

void FreeAST(CommandAST* cmd);

#endif /* KSH_AST_H */
