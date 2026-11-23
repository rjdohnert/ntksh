#include <ksh/common.h>
#include <ksh/ast.h>
#include <ksh/ksh.h>
#include <ksh/builtins.h>
#include <ksh/help.h>
#include <wctype.h>
#include <wchar.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>

/* ========================================================================= */
/*              Category: diff (POSIX / BSD compatible)                      */
/* ========================================================================= */

enum {
    DIFF_FMT_NORMAL = 0,
    DIFF_FMT_UNIFIED,
    DIFF_FMT_CONTEXT,
    DIFF_FMT_BRIEF,
    DIFF_FMT_ED,
    DIFF_FMT_FORWARD_ED,
    DIFF_FMT_RCS,
    DIFF_FMT_SIDE_BY_SIDE
};

typedef struct {
    int format;
    int context_lines;
    bool ignore_case;
    bool ignore_space_all;
    bool ignore_space_change;
    bool ignore_blank_lines;
    bool text_mode;
    bool report_identical;
    bool initial_tab;
    bool show_c_function;
    bool recursive;
    bool new_file;
    const wchar_t* starting_file;
    const wchar_t* exclude_pattern;
} DiffOpts;

enum {
    DIFF_OP_EQUAL = 0,
    DIFF_OP_DELETE,
    DIFF_OP_INSERT
};

typedef struct {
    int type;
    size_t line1; /* 1-based line number in file 1 (0 if insert) */
    size_t line2; /* 1-based line number in file 2 (0 if delete) */
    const wchar_t* text1;
    const wchar_t* text2;
} DiffEdit;

static bool DiffLineIsBlank(const wchar_t* s) {
    if (!s) return true;
    while (*s) {
        if (!iswspace(*s)) return false;
        s++;
    }
    return true;
}

static bool DiffLinesMatch(const wchar_t* s1, const wchar_t* s2, const DiffOpts* opts) {
    if (opts->ignore_blank_lines && DiffLineIsBlank(s1) && DiffLineIsBlank(s2)) return true;

    if (opts->ignore_space_all) {
        while (*s1 || *s2) {
            while (*s1 && iswspace(*s1)) s1++;
            while (*s2 && iswspace(*s2)) s2++;
            if (!*s1 && !*s2) return true;
            if (!*s1 || !*s2) return false;
            wchar_t c1 = opts->ignore_case ? towlower(*s1) : *s1;
            wchar_t c2 = opts->ignore_case ? towlower(*s2) : *s2;
            if (c1 != c2) return false;
            s1++; s2++;
        }
        return true;
    }

    if (opts->ignore_space_change) {
        while (*s1 || *s2) {
            if (iswspace(*s1) && iswspace(*s2)) {
                while (iswspace(*s1)) s1++;
                while (iswspace(*s2)) s2++;
                continue;
            }
            if (iswspace(*s1) || iswspace(*s2)) return false;
            wchar_t c1 = opts->ignore_case ? towlower(*s1) : *s1;
            wchar_t c2 = opts->ignore_case ? towlower(*s2) : *s2;
            if (c1 != c2) return false;
            s1++; s2++;
        }
        return true;
    }

    if (opts->ignore_case) return _wcsicmp(s1, s2) == 0;
    return wcscmp(s1, s2) == 0;
}

static wchar_t** DiffReadLines(FILE* fp, size_t* out_count) {
    size_t cap = 256, count = 0;
    wchar_t** lines = (wchar_t**)malloc(cap * sizeof(wchar_t*));
    if (!lines) { *out_count = 0; return NULL; }

    wchar_t buf[4096];
    while (fgetws(buf, sizeof(buf) / sizeof(buf[0]), fp)) {
        size_t len = wcslen(buf);
        while (len > 0 && (buf[len - 1] == L'\r' || buf[len - 1] == L'\n')) buf[--len] = L'\0';
        if (count >= cap) {
            size_t ncap = cap * 2;
            wchar_t** nlines = (wchar_t**)realloc(lines, ncap * sizeof(wchar_t*));
            if (!nlines) break;
            lines = nlines;
            cap = ncap;
        }
        wchar_t* dup = _wcsdup(buf);
        if (dup) lines[count++] = dup;
    }
    *out_count = count;
    return lines;
}

static void DiffFreeLines(wchar_t** lines, size_t count) {
    if (!lines) return;
    for (size_t i = 0; i < count; i++) free(lines[i]);
    free(lines);
}

/* Builds an edit script using LCS dynamic programming with fallback */
static DiffEdit* DiffBuildEdits(wchar_t* const* lines1, size_t count1,
                                wchar_t* const* lines2, size_t count2,
                                const DiffOpts* opts, size_t* out_nedits) {
    bool can_dp = (count1 <= 2000 && count2 <= 2000);
    size_t matrix_size = can_dp ? (count1 + 1) * (count2 + 1) : 0;
    int* dp = can_dp ? (int*)malloc(matrix_size * sizeof(int)) : NULL;

    size_t cap = count1 + count2 + 16;
    size_t nedits = 0;
    DiffEdit* edits = (DiffEdit*)malloc(cap * sizeof(DiffEdit));
    if (!edits) { if (dp) free(dp); *out_nedits = 0; return NULL; }

    if (dp) {
        size_t stride = count2 + 1;
        for (size_t i = 0; i <= count1; i++) dp[i * stride + count2] = 0;
        for (size_t j = 0; j <= count2; j++) dp[count1 * stride + j] = 0;

        for (size_t i = count1; i-- > 0;) {
            for (size_t j = count2; j-- > 0;) {
                if (DiffLinesMatch(lines1[i], lines2[j], opts)) {
                    dp[i * stride + j] = dp[(i + 1) * stride + (j + 1)] + 1;
                } else {
                    int down = dp[(i + 1) * stride + j];
                    int right = dp[i * stride + (j + 1)];
                    dp[i * stride + j] = (down >= right) ? down : right;
                }
            }
        }

        size_t i = 0, j = 0;
        while (i < count1 || j < count2) {
            if (nedits + 2 >= cap) {
                cap *= 2;
                DiffEdit* new_edits = (DiffEdit*)realloc(edits, cap * sizeof(DiffEdit));
                if (!new_edits) {
                    free(dp);
                    free(edits);
                    *out_nedits = 0;
                    return NULL;
                }
                edits = new_edits;
            }
            if (i < count1 && j < count2) {
                if (DiffLinesMatch(lines1[i], lines2[j], opts)) {
                    edits[nedits].type = DIFF_OP_EQUAL;
                    edits[nedits].line1 = i + 1;
                    edits[nedits].line2 = j + 1;
                    edits[nedits].text1 = lines1[i];
                    edits[nedits].text2 = lines2[j];
                    nedits++;
                    i++; j++;
                } else if (dp[(i + 1) * stride + j] >= dp[i * stride + (j + 1)]) {
                    edits[nedits].type = DIFF_OP_DELETE;
                    edits[nedits].line1 = i + 1;
                    edits[nedits].line2 = 0;
                    edits[nedits].text1 = lines1[i];
                    edits[nedits].text2 = NULL;
                    nedits++;
                    i++;
                } else {
                    edits[nedits].type = DIFF_OP_INSERT;
                    edits[nedits].line1 = 0;
                    edits[nedits].line2 = j + 1;
                    edits[nedits].text1 = NULL;
                    edits[nedits].text2 = lines2[j];
                    nedits++;
                    j++;
                }
            } else if (i < count1) {
                edits[nedits].type = DIFF_OP_DELETE;
                edits[nedits].line1 = i + 1;
                edits[nedits].line2 = 0;
                edits[nedits].text1 = lines1[i];
                edits[nedits].text2 = NULL;
                nedits++;
                i++;
            } else {
                edits[nedits].type = DIFF_OP_INSERT;
                edits[nedits].line1 = 0;
                edits[nedits].line2 = j + 1;
                edits[nedits].text1 = NULL;
                edits[nedits].text2 = lines2[j];
                nedits++;
                j++;
            }
        }
        free(dp);
    } else {
        /* Greedy lookahead fallback */
        size_t i = 0, j = 0;
        while (i < count1 || j < count2) {
            if (nedits + 4 >= cap) {
                cap *= 2;
                DiffEdit* new_edits = (DiffEdit*)realloc(edits, cap * sizeof(DiffEdit));
                if (!new_edits) {
                    free(edits);
                    *out_nedits = 0;
                    return NULL;
                }
                edits = new_edits;
            }
            if (i < count1 && j < count2) {
                if (DiffLinesMatch(lines1[i], lines2[j], opts)) {
                    edits[nedits].type = DIFF_OP_EQUAL;
                    edits[nedits].line1 = i + 1;
                    edits[nedits].line2 = j + 1;
                    edits[nedits].text1 = lines1[i];
                    edits[nedits].text2 = lines2[j];
                    nedits++;
                    i++; j++;
                } else {
                    int f1 = -1, f2 = -1;
                    for (size_t k = 1; k <= 64; k++) {
                        if (f1 == -1 && i + k < count1 && DiffLinesMatch(lines1[i + k], lines2[j], opts)) f1 = (int)k;
                        if (f2 == -1 && j + k < count2 && DiffLinesMatch(lines1[i], lines2[j + k], opts)) f2 = (int)k;
                        if (f1 != -1 || f2 != -1) break;
                    }
                    if (f1 != -1 && (f2 == -1 || f1 <= f2)) {
                        edits[nedits].type = DIFF_OP_DELETE;
                        edits[nedits].line1 = i + 1;
                        edits[nedits].line2 = 0;
                        edits[nedits].text1 = lines1[i++];
                        edits[nedits].text2 = NULL;
                        nedits++;
                    } else if (f2 != -1) {
                        edits[nedits].type = DIFF_OP_INSERT;
                        edits[nedits].line1 = 0;
                        edits[nedits].line2 = j + 1;
                        edits[nedits].text1 = NULL;
                        edits[nedits].text2 = lines2[j++];
                        nedits++;
                    } else {
                        edits[nedits].type = DIFF_OP_DELETE;
                        edits[nedits].line1 = i + 1;
                        edits[nedits].line2 = 0;
                        edits[nedits].text1 = lines1[i++];
                        edits[nedits].text2 = NULL;
                        nedits++;

                        edits[nedits].type = DIFF_OP_INSERT;
                        edits[nedits].line1 = 0;
                        edits[nedits].line2 = j + 1;
                        edits[nedits].text1 = NULL;
                        edits[nedits].text2 = lines2[j++];
                        nedits++;
                    }
                }
            } else if (i < count1) {
                edits[nedits].type = DIFF_OP_DELETE;
                edits[nedits].line1 = i + 1;
                edits[nedits].line2 = 0;
                edits[nedits].text1 = lines1[i++];
                edits[nedits].text2 = NULL;
                nedits++;
            } else {
                edits[nedits].type = DIFF_OP_INSERT;
                edits[nedits].line1 = 0;
                edits[nedits].line2 = j + 1;
                edits[nedits].text1 = NULL;
                edits[nedits].text2 = lines2[j++];
                nedits++;
            }
        }
    }
    *out_nedits = nedits;
    return edits;
}

/* Outputs standard BSD Unified Diff with proper hunk calculation */
static void DiffPrintUnified(const wchar_t* file1, const wchar_t* file2,
                             const DiffEdit* edits, size_t nedits, int ctx, bool tab) {
    const wchar_t* pfx = tab ? L"\t" : L"";
    wprintf(L"--- %ls\n+++ %ls\n", file1, file2);

    size_t i = 0;
    while (i < nedits) {
        /* Find next difference */
        while (i < nedits && edits[i].type == DIFF_OP_EQUAL) i++;
        if (i >= nedits) break;

        /* Hunk start */
        size_t hstart = (i >= (size_t)ctx) ? (i - (size_t)ctx) : 0;
        size_t hend = i;

        /* Extend hunk while differences are close together */
        while (hend < nedits) {
            if (edits[hend].type != DIFF_OP_EQUAL) {
                hend++;
            } else {
                size_t eq_run = 0;
                size_t k = hend;
                while (k < nedits && edits[k].type == DIFF_OP_EQUAL) { eq_run++; k++; }
                if (k < nedits && eq_run <= (size_t)(2 * ctx)) {
                    hend = k;
                } else {
                    hend += (eq_run < (size_t)ctx ? eq_run : (size_t)ctx);
                    break;
                }
            }
        }

        /* Calculate line ranges for hunk */
        size_t l1_start = 0, l1_count = 0, l2_start = 0, l2_count = 0;
        for (size_t k = hstart; k < hend; k++) {
            if (edits[k].type == DIFF_OP_EQUAL || edits[k].type == DIFF_OP_DELETE) {
                if (l1_start == 0) l1_start = edits[k].line1;
                l1_count++;
            }
            if (edits[k].type == DIFF_OP_EQUAL || edits[k].type == DIFF_OP_INSERT) {
                if (l2_start == 0) l2_start = edits[k].line2;
                l2_count++;
            }
        }
        if (l1_start == 0) l1_start = (hstart > 0) ? edits[hstart - 1].line1 : 1;
        if (l2_start == 0) l2_start = (hstart > 0) ? edits[hstart - 1].line2 : 1;

        wprintf(L"@@ -%zu,%zu +%zu,%zu @@\n", l1_start, l1_count, l2_start, l2_count);
        for (size_t k = hstart; k < hend; k++) {
            if (edits[k].type == DIFF_OP_EQUAL) {
                wprintf(L" %ls%ls\n", pfx, edits[k].text1);
            } else if (edits[k].type == DIFF_OP_DELETE) {
                wprintf(L"-%ls%ls\n", pfx, edits[k].text1);
            } else {
                wprintf(L"+%ls%ls\n", pfx, edits[k].text2);
            }
        }
        i = hend;
    }
}

/* Outputs standard BSD Context Diff */
static void DiffPrintContext(const wchar_t* file1, const wchar_t* file2,
                             const DiffEdit* edits, size_t nedits, int ctx, bool tab) {
    const wchar_t* pfx = tab ? L"\t" : L"";
    wprintf(L"*** %ls\n--- %ls\n***************\n", file1, file2);

    size_t i = 0;
    while (i < nedits) {
        while (i < nedits && edits[i].type == DIFF_OP_EQUAL) i++;
        if (i >= nedits) break;

        size_t hstart = (i >= (size_t)ctx) ? (i - (size_t)ctx) : 0;
        size_t hend = i;

        while (hend < nedits) {
            if (edits[hend].type != DIFF_OP_EQUAL) hend++;
            else {
                size_t eq_run = 0, k = hend;
                while (k < nedits && edits[k].type == DIFF_OP_EQUAL) { eq_run++; k++; }
                if (k < nedits && eq_run <= (size_t)(2 * ctx)) hend = k;
                else { hend += (eq_run < (size_t)ctx ? eq_run : (size_t)ctx); break; }
            }
        }

        size_t l1_start = 0, l1_end = 0, l2_start = 0, l2_end = 0;
        bool has_del = false, has_ins = false;
        for (size_t k = hstart; k < hend; k++) {
            if (edits[k].type == DIFF_OP_EQUAL || edits[k].type == DIFF_OP_DELETE) {
                if (l1_start == 0) l1_start = edits[k].line1;
                l1_end = edits[k].line1;
                if (edits[k].type == DIFF_OP_DELETE) has_del = true;
            }
            if (edits[k].type == DIFF_OP_EQUAL || edits[k].type == DIFF_OP_INSERT) {
                if (l2_start == 0) l2_start = edits[k].line2;
                l2_end = edits[k].line2;
                if (edits[k].type == DIFF_OP_INSERT) has_ins = true;
            }
        }

        wprintf(L"*** %zu,%zu ****\n", l1_start, l1_end);
        for (size_t k = hstart; k < hend; k++) {
            if (edits[k].type == DIFF_OP_EQUAL) {
                wprintf(L"  %ls%ls\n", pfx, edits[k].text1);
            } else if (edits[k].type == DIFF_OP_DELETE) {
                wprintf(L"%lc %ls%ls\n", (has_ins ? L'!' : L'-'), pfx, edits[k].text1);
            }
        }

        wprintf(L"--- %zu,%zu ----\n", l2_start, l2_end);
        for (size_t k = hstart; k < hend; k++) {
            if (edits[k].type == DIFF_OP_EQUAL) {
                wprintf(L"  %ls%ls\n", pfx, edits[k].text2);
            } else if (edits[k].type == DIFF_OP_INSERT) {
                wprintf(L"%lc %ls%ls\n", (has_del ? L'!' : L'+'), pfx, edits[k].text2);
            }
        }
        i = hend;
    }
}

/* Outputs traditional BSD / POSIX Normal Diff */
static void DiffPrintNormal(const DiffEdit* edits, size_t nedits, bool tab) {
    const wchar_t* pfx = tab ? L"\t" : L"";
    size_t i = 0;
    while (i < nedits) {
        while (i < nedits && edits[i].type == DIFF_OP_EQUAL) i++;
        if (i >= nedits) break;

        size_t d_start = 0, d_end = 0, a_start = 0, a_end = 0;
        size_t blk_start = i;

        while (i < nedits && edits[i].type != DIFF_OP_EQUAL) {
            if (edits[i].type == DIFF_OP_DELETE) {
                if (d_start == 0) d_start = edits[i].line1;
                d_end = edits[i].line1;
            } else if (edits[i].type == DIFF_OP_INSERT) {
                if (a_start == 0) a_start = edits[i].line2;
                a_end = edits[i].line2;
            }
            i++;
        }
        size_t blk_end = i;

        if (d_start > 0 && a_start == 0) {
            /* Deletion: s1[,e1] d s2 */
            size_t s2 = (blk_start > 0) ? edits[blk_start - 1].line2 : 0;
            if (d_start == d_end) wprintf(L"%zud%zu\n", d_start, s2);
            else wprintf(L"%zu,%zud%zu\n", d_start, d_end, s2);
            for (size_t k = blk_start; k < blk_end; k++) {
                if (edits[k].type == DIFF_OP_DELETE) wprintf(L"<%ls %ls\n", pfx, edits[k].text1);
            }
        } else if (d_start == 0 && a_start > 0) {
            /* Addition: s1 a s2[,e2] */
            size_t s1 = (blk_start > 0) ? edits[blk_start - 1].line1 : 0;
            if (a_start == a_end) wprintf(L"%zua%zu\n", s1, a_start);
            else wprintf(L"%zua%zu,%zu\n", s1, a_start, a_end);
            for (size_t k = blk_start; k < blk_end; k++) {
                if (edits[k].type == DIFF_OP_INSERT) wprintf(L">%ls %ls\n", pfx, edits[k].text2);
            }
        } else {
            /* Change: s1[,e1] c s2[,e2] */
            if (d_start == d_end) wprintf(L"%zuc", d_start);
            else wprintf(L"%zu,%zuc", d_start, d_end);
            if (a_start == a_end) wprintf(L"%zu\n", a_start);
            else wprintf(L"%zu,%zu\n", a_start, a_end);
            for (size_t k = blk_start; k < blk_end; k++) {
                if (edits[k].type == DIFF_OP_DELETE) wprintf(L"<%ls %ls\n", pfx, edits[k].text1);
            }
            wprintf(L"---\n");
            for (size_t k = blk_start; k < blk_end; k++) {
                if (edits[k].type == DIFF_OP_INSERT) wprintf(L">%ls %ls\n", pfx, edits[k].text2);
            }
        }
    }
}

/* Outputs Ed script (-e) */
static void DiffPrintEd(const DiffEdit* edits, size_t nedits) {
    if (nedits == 0) return;
    size_t i = nedits;
    while (i > 0) {
        while (i > 0 && edits[i - 1].type == DIFF_OP_EQUAL) i--;
        if (i == 0) break;

        size_t blk_end = i;
        size_t d_start = 0, d_end = 0, a_start = 0, a_end = 0;
        while (i > 0 && edits[i - 1].type != DIFF_OP_EQUAL) {
            i--;
            if (edits[i].type == DIFF_OP_DELETE) {
                if (d_end == 0) d_end = edits[i].line1;
                d_start = edits[i].line1;
            } else {
                if (a_end == 0) a_end = edits[i].line2;
                a_start = edits[i].line2;
            }
        }
        size_t blk_start = i;

        if (d_start > 0 && a_start == 0) {
            if (d_start == d_end) wprintf(L"%zud\n", d_start);
            else wprintf(L"%zu,%zud\n", d_start, d_end);
        } else if (d_start == 0 && a_start > 0) {
            size_t s1 = (blk_start > 0) ? edits[blk_start - 1].line1 : 0;
            wprintf(L"%zua\n", s1);
            for (size_t k = blk_start; k < blk_end; k++) {
                if (edits[k].type == DIFF_OP_INSERT) wprintf(L"%ls\n", edits[k].text2);
            }
            wprintf(L".\n");
        } else {
            if (d_start == d_end) wprintf(L"%zuc\n", d_start);
            else wprintf(L"%zu,%zuc\n", d_start, d_end);
            for (size_t k = blk_start; k < blk_end; k++) {
                if (edits[k].type == DIFF_OP_INSERT) wprintf(L"%ls\n", edits[k].text2);
            }
            wprintf(L".\n");
        }
    }
}

/* Outputs Side by Side diff (-y) */
static void DiffPrintSideBySide(const DiffEdit* edits, size_t nedits, int term_width) {
    int col_w = (term_width - 3) / 2;
    if (col_w < 10) col_w = 38;

    for (size_t i = 0; i < nedits; i++) {
        if (edits[i].type == DIFF_OP_EQUAL) {
            wprintf(L"%-*.*ls   %-*.*ls\n", col_w, col_w, edits[i].text1, col_w, col_w, edits[i].text2);
        } else if (edits[i].type == DIFF_OP_DELETE) {
            wprintf(L"%-*.*ls <\n", col_w, col_w, edits[i].text1);
        } else {
            wprintf(L"%-*ls > %-*.*ls\n", col_w, L"", col_w, col_w, edits[i].text2);
        }
    }
}

/* Compares two files and returns 0 if identical, 1 if differ, 2 on error */
static int DiffTwoFiles(const wchar_t* file1, const wchar_t* file2, const DiffOpts* opts) {
    bool is_stdin1 = wcscmp(file1, L"-") == 0;
    bool is_stdin2 = wcscmp(file2, L"-") == 0;

    FILE* fp1 = is_stdin1 ? stdin : _wfsopen(file1, L"rt, ccs=UTF-8", _SH_DENYNO);
    if (!fp1 && !is_stdin1) fp1 = _wfsopen(file1, L"r", _SH_DENYNO);

    FILE* fp2 = is_stdin2 ? stdin : _wfsopen(file2, L"rt, ccs=UTF-8", _SH_DENYNO);
    if (!fp2 && !is_stdin2) fp2 = _wfsopen(file2, L"r", _SH_DENYNO);

    if (!fp1 || !fp2) {
        if (!fp1 && !opts->new_file) fwprintf(stderr, L"diff: %ls: No such file or directory\n", file1);
        if (!fp2 && !opts->new_file) fwprintf(stderr, L"diff: %ls: No such file or directory\n", file2);
        if (fp1 && fp1 != stdin) fclose(fp1);
        if (fp2 && fp2 != stdin) fclose(fp2);
        return 2;
    }

    size_t count1 = 0, count2 = 0;
    wchar_t** lines1 = fp1 ? DiffReadLines(fp1, &count1) : NULL;
    wchar_t** lines2 = fp2 ? DiffReadLines(fp2, &count2) : NULL;

    if (fp1 && fp1 != stdin) fclose(fp1);
    if (fp2 && fp2 != stdin) fclose(fp2);

    /* Check identical */
    bool identical = (count1 == count2);
    if (identical) {
        for (size_t k = 0; k < count1; k++) {
            if (!DiffLinesMatch(lines1[k], lines2[k], opts)) {
                identical = false;
                break;
            }
        }
    }

    if (identical) {
        if (opts->report_identical) wprintf(L"Files %ls and %ls are identical\n", file1, file2);
        DiffFreeLines(lines1, count1);
        DiffFreeLines(lines2, count2);
        return 0;
    }

    if (opts->format == DIFF_FMT_BRIEF) {
        wprintf(L"Files %ls and %ls differ\n", file1, file2);
        DiffFreeLines(lines1, count1);
        DiffFreeLines(lines2, count2);
        return 1;
    }

    size_t nedits = 0;
    DiffEdit* edits = DiffBuildEdits(lines1, count1, lines2, count2, opts, &nedits);

    switch (opts->format) {
    case DIFF_FMT_UNIFIED:
        DiffPrintUnified(file1, file2, edits, nedits, opts->context_lines, opts->initial_tab);
        break;
    case DIFF_FMT_CONTEXT:
        DiffPrintContext(file1, file2, edits, nedits, opts->context_lines, opts->initial_tab);
        break;
    case DIFF_FMT_ED:
        DiffPrintEd(edits, nedits);
        break;
    case DIFF_FMT_SIDE_BY_SIDE: {
        int tw = 80;
        CONSOLE_SCREEN_BUFFER_INFO csbi;
        if (GetConsoleScreenBufferInfo(GetStdHandle(STD_OUTPUT_HANDLE), &csbi)) {
            int w = csbi.srWindow.Right - csbi.srWindow.Left + 1;
            if (w > 20) tw = w;
        }
        DiffPrintSideBySide(edits, nedits, tw);
        break;
    }
    default:
        DiffPrintNormal(edits, nedits, opts->initial_tab);
        break;
    }

    free(edits);
    DiffFreeLines(lines1, count1);
    DiffFreeLines(lines2, count2);
    return 1;
}

/* Recursively diff directories */
static int DiffDirectories(const wchar_t* dir1, const wchar_t* dir2, const DiffOpts* opts);

static int DiffDirectoryEntry(const wchar_t* dir1, const wchar_t* dir2, const wchar_t* name, const DiffOpts* opts) {
    wchar_t p1[KSH_PATH_MAX], p2[KSH_PATH_MAX];
    swprintf_s(p1, _countof(p1), L"%ls\\%ls", dir1, name);
    swprintf_s(p2, _countof(p2), L"%ls\\%ls", dir2, name);

    DWORD a1 = GetFileAttributesW(p1);
    DWORD a2 = GetFileAttributesW(p2);

    if (a1 == INVALID_FILE_ATTRIBUTES && a2 == INVALID_FILE_ATTRIBUTES) return 0;
    if (a1 == INVALID_FILE_ATTRIBUTES) {
        if (opts->new_file) return DiffTwoFiles(p1, p2, opts);
        wprintf(L"Only in %ls: %ls\n", dir2, name);
        return 1;
    }
    if (a2 == INVALID_FILE_ATTRIBUTES) {
        if (opts->new_file) return DiffTwoFiles(p1, p2, opts);
        wprintf(L"Only in %ls: %ls\n", dir1, name);
        return 1;
    }

    bool d1 = (a1 & FILE_ATTRIBUTE_DIRECTORY) != 0;
    bool d2 = (a2 & FILE_ATTRIBUTE_DIRECTORY) != 0;

    if (d1 && d2) {
        if (opts->recursive) return DiffDirectories(p1, p2, opts);
        wprintf(L"Common subdirectories: %ls and %ls\n", p1, p2);
        return 0;
    }
    if (d1 != d2) {
        wprintf(L"File %ls is a %ls while file %ls is a %ls\n",
                p1, d1 ? L"directory" : L"regular file",
                p2, d2 ? L"directory" : L"regular file");
        return 1;
    }
    return DiffTwoFiles(p1, p2, opts);
}

static int DiffDirectories(const wchar_t* dir1, const wchar_t* dir2, const DiffOpts* opts) {
    wchar_t mask1[KSH_PATH_MAX];
    swprintf_s(mask1, _countof(mask1), L"%ls\\*", dir1);
    WIN32_FIND_DATAW fd;
    HANDLE h1 = FindFirstFileW(mask1, &fd);
    int status = 0;

    if (h1 != INVALID_HANDLE_VALUE) {
        do {
            if (wcscmp(fd.cFileName, L".") == 0 || wcscmp(fd.cFileName, L"..") == 0) continue;
            int r = DiffDirectoryEntry(dir1, dir2, fd.cFileName, opts);
            if (r > status) status = r;
        } while (FindNextFileW(h1, &fd));
        FindClose(h1);
    }

    /* Check files only in dir2 */
    wchar_t mask2[KSH_PATH_MAX];
    swprintf_s(mask2, _countof(mask2), L"%ls\\*", dir2);
    HANDLE h2 = FindFirstFileW(mask2, &fd);
    if (h2 != INVALID_HANDLE_VALUE) {
        do {
            if (wcscmp(fd.cFileName, L".") == 0 || wcscmp(fd.cFileName, L"..") == 0) continue;
            wchar_t p1[KSH_PATH_MAX];
            swprintf_s(p1, _countof(p1), L"%ls\\%ls", dir1, fd.cFileName);
            if (GetFileAttributesW(p1) == INVALID_FILE_ATTRIBUTES) {
                if (opts->new_file) {
                    wchar_t p2[KSH_PATH_MAX];
                    swprintf_s(p2, _countof(p2), L"%ls\\%ls", dir2, fd.cFileName);
                    int r = DiffTwoFiles(p1, p2, opts);
                    if (r > status) status = r;
                } else {
                    wprintf(L"Only in %ls: %ls\n", dir2, fd.cFileName);
                    if (status < 1) status = 1;
                }
            }
        } while (FindNextFileW(h2, &fd));
        FindClose(h2);
    }
    return status;
}

bool Builtin_Diff(CommandAST* cmd, int* code) {
    if (cmd->argc > 1 && (_wcsicmp(cmd->argv[1], L"/?") == 0 || wcscmp(cmd->argv[1], L"--help") == 0)) {
        wprintf(L"Usage: diff [OPTION]... FILES\n"
                L"Compare FILES line by line (BSD/POSIX compatible).\n\n"
                L"  -u, -U NUM, --unified[=NUM]   Output unified context diff (default 3 lines)\n"
                L"  -c, -C NUM, --context[=NUM]   Output standard context diff (default 3 lines)\n"
                L"  -q, --brief                   Report only when files differ\n"
                L"  -e, --ed                      Output an ed script\n"
                L"  -f, --forward-ed              Output a forward ed script\n"
                L"  -n, --rcs                     Output an RCS format diff\n"
                L"  -y, --side-by-side            Output in two columns\n"
                L"  -i, --ignore-case             Ignore case differences in file contents\n"
                L"  -w, --ignore-all-space        Ignore all white space\n"
                L"  -b, --ignore-space-change     Ignore changes in the amount of white space\n"
                L"  -B, --ignore-blank-lines      Ignore changes where lines are all blank\n"
                L"  -a, --text                    Treat all files as text\n"
                L"  -s, --report-identical-files  Report when two files are identical\n"
                L"  -T, --initial-tab             Make tabs line up by prepending a tab\n"
                L"  -p, --show-c-function         Show which C function each change is in\n"
                L"  -r, --recursive               Recursively compare any subdirectories found\n"
                L"  -N, --new-file                Treat absent files as empty\n"
                L"  -V, --version                 Output version information\n");
        *code = 0;
        return true;
    }

    DiffOpts opts;
    memset(&opts, 0, sizeof(opts));
    opts.format = DIFF_FMT_NORMAL;
    opts.context_lines = 3;

    const wchar_t* file1 = NULL;
    const wchar_t* file2 = NULL;
    bool opts_done = false;

    for (int i = 1; i < cmd->argc; i++) {
        const wchar_t* arg = cmd->argv[i];
        if (opts_done || arg[0] != L'-' || wcscmp(arg, L"-") == 0) {
            if (!file1) file1 = arg;
            else if (!file2) file2 = arg;
            continue;
        }
        if (wcscmp(arg, L"--") == 0) {
            opts_done = true;
            continue;
        }
        if (wcscmp(arg, L"-V") == 0 || wcscmp(arg, L"--version") == 0) {
            wprintf(L"diff (BSD-compatible) 1.0 (ntksh)\n");
            *code = 0;
            return true;
        }

        if (arg[1] == L'-') {
            if (wcscmp(arg, L"--brief") == 0) opts.format = DIFF_FMT_BRIEF;
            else if (wcscmp(arg, L"--ed") == 0) opts.format = DIFF_FMT_ED;
            else if (wcscmp(arg, L"--forward-ed") == 0) opts.format = DIFF_FMT_FORWARD_ED;
            else if (wcscmp(arg, L"--rcs") == 0) opts.format = DIFF_FMT_RCS;
            else if (wcscmp(arg, L"--side-by-side") == 0) opts.format = DIFF_FMT_SIDE_BY_SIDE;
            else if (wcscmp(arg, L"--ignore-case") == 0) opts.ignore_case = true;
            else if (wcscmp(arg, L"--ignore-all-space") == 0) opts.ignore_space_all = true;
            else if (wcscmp(arg, L"--ignore-space-change") == 0) opts.ignore_space_change = true;
            else if (wcscmp(arg, L"--ignore-blank-lines") == 0) opts.ignore_blank_lines = true;
            else if (wcscmp(arg, L"--text") == 0) opts.text_mode = true;
            else if (wcscmp(arg, L"--report-identical-files") == 0) opts.report_identical = true;
            else if (wcscmp(arg, L"--initial-tab") == 0) opts.initial_tab = true;
            else if (wcscmp(arg, L"--show-c-function") == 0) opts.show_c_function = true;
            else if (wcscmp(arg, L"--recursive") == 0) opts.recursive = true;
            else if (wcscmp(arg, L"--new-file") == 0) opts.new_file = true;
            else if (wcsncmp(arg, L"--unified", 9) == 0) {
                opts.format = DIFF_FMT_UNIFIED;
                if (arg[9] == L'=') opts.context_lines = _wtoi(arg + 10);
            } else if (wcsncmp(arg, L"--context", 9) == 0) {
                opts.format = DIFF_FMT_CONTEXT;
                if (arg[9] == L'=') opts.context_lines = _wtoi(arg + 10);
            }
            continue;
        }

        /* Short options cluster */
        for (int j = 1; arg[j] != L'\0'; j++) {
            wchar_t c = arg[j];
            if (c == L'u') {
                opts.format = DIFF_FMT_UNIFIED;
            } else if (c == L'U') {
                opts.format = DIFF_FMT_UNIFIED;
                if (arg[j + 1] != L'\0') {
                    opts.context_lines = _wtoi(arg + j + 1);
                    break;
                } else if (i + 1 < cmd->argc && iswdigit(cmd->argv[i + 1][0])) {
                    opts.context_lines = _wtoi(cmd->argv[++i]);
                    break;
                }
            } else if (c == L'c') {
                opts.format = DIFF_FMT_CONTEXT;
            } else if (c == L'C') {
                opts.format = DIFF_FMT_CONTEXT;
                if (arg[j + 1] != L'\0') {
                    opts.context_lines = _wtoi(arg + j + 1);
                    break;
                } else if (i + 1 < cmd->argc && iswdigit(cmd->argv[i + 1][0])) {
                    opts.context_lines = _wtoi(cmd->argv[++i]);
                    break;
                }
            } else if (c == L'q') {
                opts.format = DIFF_FMT_BRIEF;
            } else if (c == L'e') {
                opts.format = DIFF_FMT_ED;
            } else if (c == L'f') {
                opts.format = DIFF_FMT_FORWARD_ED;
            } else if (c == L'n') {
                opts.format = DIFF_FMT_RCS;
            } else if (c == L'y') {
                opts.format = DIFF_FMT_SIDE_BY_SIDE;
            } else if (c == L'i') {
                opts.ignore_case = true;
            } else if (c == L'w') {
                opts.ignore_space_all = true;
            } else if (c == L'b') {
                opts.ignore_space_change = true;
            } else if (c == L'B') {
                opts.ignore_blank_lines = true;
            } else if (c == L'a') {
                opts.text_mode = true;
            } else if (c == L's') {
                opts.report_identical = true;
            } else if (c == L'T') {
                opts.initial_tab = true;
            } else if (c == L'p') {
                opts.show_c_function = true;
            } else if (c == L'r') {
                opts.recursive = true;
            } else if (c == L'N') {
                opts.new_file = true;
            }
        }
    }

    if (!file1 || !file2) {
        fwprintf(stderr, L"diff: missing operand\n");
        *code = 2;
        return true;
    }

    DWORD a1 = (wcscmp(file1, L"-") == 0) ? 0 : GetFileAttributesW(file1);
    DWORD a2 = (wcscmp(file2, L"-") == 0) ? 0 : GetFileAttributesW(file2);

    bool is_dir1 = (a1 != INVALID_FILE_ATTRIBUTES) && (a1 & FILE_ATTRIBUTE_DIRECTORY);
    bool is_dir2 = (a2 != INVALID_FILE_ATTRIBUTES) && (a2 & FILE_ATTRIBUTE_DIRECTORY);

    if (is_dir1 && is_dir2) {
        *code = DiffDirectories(file1, file2, &opts);
        return true;
    }

    *code = DiffTwoFiles(file1, file2, &opts);
    return true;
}
