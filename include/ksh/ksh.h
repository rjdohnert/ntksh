#ifndef KSH_KSH_H
#define KSH_KSH_H

#include <ksh/common.h>
#include <ksh/ast.h>

typedef struct IEnvironment IEnvironment;
typedef struct IParser IParser;
typedef struct IExecutor IExecutor;
typedef struct IShell IShell;

typedef struct EnvEntry {
    wchar_t* key;
    wchar_t* value;
    bool readonly;
    bool integer;
    bool is_float;
    bool float_exponential;
    unsigned int float_precision;
    bool lowercase;
    bool uppercase;
    bool is_nameref;
    wchar_t alignment;
    unsigned int integerBase;
    unsigned int width;
    struct EnvEntry* next;
} EnvEntry;

typedef struct ShellLimit {
    wchar_t option;
    const wchar_t* name;
    unsigned long long soft;
    unsigned long long hard;
} ShellLimit;

typedef struct ArrayElement {
    wchar_t* key;
    wchar_t* value;
    struct ArrayElement* next;
} ArrayElement;

typedef struct ShellArray {
    wchar_t* name;
    bool associative;
    size_t count;
    ArrayElement* elements;
    struct ShellArray* next;
} ShellArray;

typedef struct ShellFunction {
    wchar_t* name;
    wchar_t* body;
    struct ShellFunction* next;
} ShellFunction;

typedef struct ShellTypeMember {
    wchar_t name[128];
    wchar_t defaultValue[MAX_LINE_LEN];
    bool isFunction;
    wchar_t* functionBody;
    struct ShellTypeMember* next;
} ShellTypeMember;

typedef struct ShellTypeDef {
    wchar_t name[128];
    ShellTypeMember* members;
    struct ShellTypeDef* next;
} ShellTypeDef;

typedef struct ShellAlias {
    wchar_t* name;
    wchar_t* value;
    struct ShellAlias* next;
} ShellAlias;

typedef struct DirectoryStackEntry {
    wchar_t* path;
    wchar_t mappedDrive[4];
    struct DirectoryStackEntry* next;
} DirectoryStackEntry;

typedef struct LocalBinding {
    EnvEntry* entry;
    struct LocalBinding* next;
} LocalBinding;

typedef struct LocalScope {
    LocalBinding* bindings;
    struct LocalScope* parent;
} LocalScope;

typedef struct BatchLoopVariable {
    wchar_t name;
    wchar_t* value;
    struct BatchLoopVariable* next;
} BatchLoopVariable;

typedef struct BatchVariableChange {
    wchar_t* name;
    wchar_t* value;
    bool existed;
    struct BatchVariableChange* next;
} BatchVariableChange;

typedef struct ProcessSubstEntry {
    wchar_t tempPath[MAX_PATH];
    wchar_t cmd[MAX_LINE_LEN];
    bool isOutput;
    struct ProcessSubstEntry* next;
} ProcessSubstEntry;

typedef struct BatchLocalFrame {
    BatchVariableChange* changes;
    bool delayedExpansion;
    struct BatchLocalFrame* parent;
} BatchLocalFrame;

typedef struct BatchContext {
    wchar_t* scriptPath;
    wchar_t* arguments[MAX_ARGS];
    int argumentCount;
    int shift;
    int errorLevel;
    bool delayedExpansion;
    bool echoEnabled;
    BatchLoopVariable* loopVariables;
    BatchLocalFrame* localFrame;
    struct BatchContext* parent;
    const wchar_t* batchFile;
    BatchLocalFrame* frameFloor;
    size_t currentLine;
    size_t gotoLine;
    int flow;
    int lastCommandStatus;
    bool keepErrorLevel;
} BatchContext;

typedef struct BatchFileLines {
    wchar_t** items;
    size_t count;
    size_t capacity;
} BatchFileLines;

typedef struct ShellJob {
    int id;
    DWORD pid;
    HANDLE hProcess;
    wchar_t command[512];
    bool running;
    bool stopped;
    bool disowned;
    int exit_code;
} ShellJob;

ShellJob* ResolveJobSpec(const wchar_t* spec);
void Shell_TerminateActiveJobs(void);

typedef struct ShellTrap {
    wchar_t sig[32];
    wchar_t* action;
    struct ShellTrap* next;
} ShellTrap;

struct IEnvironment {
    EnvEntry* head;
    ShellArray* arrays;
    wchar_t* positional[MAX_ARGS];
    int positionalCount;
    int lastStatus;
    bool batchMode;
    bool nounset;
    bool expansionError;
    /* When set, Expand backslash-escapes substituted values so the parser treats them as data. */
    bool escapeExpansions;
    /* When set, Expand uses here-document rules: quotes are literal, only \$ \` \\ are escapes. */
    bool heredocMode;
    ProcessSubstEntry* pendingProcSubst;
    struct IShell* shell;
    bool (*Set)(IEnvironment* self, const wchar_t* key, const wchar_t* val);
    const wchar_t* (*Get)(IEnvironment* self, const wchar_t* key);
    bool (*Unset)(IEnvironment* self, const wchar_t* key);
    wchar_t* (*Expand)(IEnvironment* self, const wchar_t* input);
    void (*Destroy)(IEnvironment* self);
};

struct IParser {
    IEnvironment* env;
    CommandAST* (*Parse)(IParser* self, const wchar_t* line);
    void (*Destroy)(IParser* self);
};

struct IExecutor {
    IShell* shell;
    int (*Execute)(IExecutor* self, CommandAST* ast);
    void (*Destroy)(IExecutor* self);
};

struct IShell {
    IEnvironment* env;
    IParser* parser;
    IExecutor* executor;
    bool running;
    int last_status;
    int sourceDepth;
    bool returnRequested;
    int returnStatus;
    int functionDepth;
    ShellFunction* functions;
    ShellAlias* aliases;
    DirectoryStackEntry* directoryStack;
    LocalScope* localScope;
    BatchContext* batchContext;
    int getoptsOffset;
    int getoptsLastOptind;
    bool batchVerify;
    bool errexit;
    bool pipefail;
    bool interactive;
    bool xtrace;
    bool noclobber;
    bool noglob;
    bool noexec;
    bool verbose;
    bool allexport;
    bool notify;
    bool monitor;
    bool hashall;
    int editMode;
    WORD defaultConsoleAttributes;
    bool capturedConsoleAttributes;
    unsigned int fileMask;
    ShellLimit limits[SHELL_LIMIT_COUNT];
    int loopBreak;
    int loopContinue;

    HANDLE coproc_in_write;
    HANDLE coproc_out_read;
    HANDLE coproc_proc;
    DWORD coproc_pid;
    bool coproc_active;

    /* Descriptors 3-9 set by exec or by fd redirections; NULL when unused. */
    HANDLE fdTable[KSH_FD_COUNT];
    bool execReplaced;
    ShellTypeDef* types;

    void (*RunREPL)(IShell* self);
    int (*RunScript)(IShell* self, const wchar_t* filename);
    int (*ExecuteLine)(IShell* self, const wchar_t* line);
    void (*Destroy)(IShell* self);
};

ShellTypeDef* Shell_FindType(IShell* shell, const wchar_t* name);
bool Shell_RegisterType(IShell* shell, const wchar_t* name, ShellTypeMember* members);

/* Core prototypes */
IEnvironment* CreateEnvironment(void);
IParser* CreateParser(IEnvironment* env);
IExecutor* CreateExecutor(IShell* shell);
IShell* CreateShell(void);

bool Env_SetPositional(IEnvironment* self, const wchar_t* zero, int argc, wchar_t* const argv[]);
bool Env_ShiftPositional(IEnvironment* self, int count);
ShellArray* Env_FindArray(IEnvironment* self, const wchar_t* name);
ShellArray* Env_CreateArray(IEnvironment* self, const wchar_t* name, bool associative);
bool Env_ArraySet(ShellArray* array, const wchar_t* key, const wchar_t* value);
bool Env_ArraySetIndex(ShellArray* array, size_t index, const wchar_t* value);
const wchar_t* Env_ArrayGet(const ShellArray* array, const wchar_t* key);
wchar_t* Env_ArrayJoin(const ShellArray* array);
void Env_ArrayClear(ShellArray* array);
bool Env_MakeReadonly(IEnvironment* self, const wchar_t* key);
EnvEntry* Env_FindEntry(IEnvironment* self, const wchar_t* key);
wchar_t* Env_TransformValue(const EnvEntry* entry, const wchar_t* val);

wchar_t* BuildWin32CommandLine(wchar_t** argv, int argc);
/* Script here-documents with unquoted delimiters are registered so the parser expands their
   bodies each time the redirection executes. */
void Ksh_HeredocRegister(const wchar_t* path);
void Ksh_HeredocUnregister(const wchar_t* path);
/* Appends word to buf (space-separated) quoted so the shell re-parses it as one literal word.
   Returns false if it does not fit. */
bool Ksh_AppendShellWord(wchar_t* buf, size_t cap, const wchar_t* word);
/* Creates an empty temp file named %TEMP%\<prefix><pid>_<n>.tmp. Tagging files with the owning
   PID lets Ksh_SweepStaleTempFiles remove ones left behind by shells that died abnormally. */
bool Ksh_CreateTempFile(const wchar_t* prefix, wchar_t* out, size_t cap);
void Ksh_SweepStaleTempFiles(void);
void Shell_UpdateJobs(void);
void Shell_UpdateJobsNotify(IShell* shell);
bool Ksh_HasPseudoConsoleSupport(void);
int OpenWideDescriptor(const wchar_t* path, int flags, int mode);
int ExecuteBuiltinWithRedirections(IShell* shell, CommandAST* cmd, HANDLE hIn, HANDLE hOut, int* outCode);
int ExecuteSingleCommand(IShell* shell, CommandAST* cmd, HANDLE hIn, HANDLE hOut);
bool ApplyFdRedirects(IShell* shell, CommandAST* cmd, bool persistent);

int Shell_ExecuteLine(IShell* self, const wchar_t* line);
int Shell_RunScript(IShell* self, const wchar_t* filename);
void Shell_RunREPL(IShell* self);
void Shell_Destroy(IShell* self);
wchar_t* Shell_CaptureCommandOutput(struct IShell* shell, const wchar_t* command);
void Shell_ProcessProcSubstCleanup(IShell* shell);
bool Shell_HasFunction(IShell* shell, const wchar_t* name);
int Shell_InvokeFunction(IShell* shell, CommandAST* cmd);
ShellFunction* Shell_FindFunction(IShell* shell, const wchar_t* name);
bool Shell_SetFunction(IShell* shell, const wchar_t* name, const wchar_t* body);
bool Shell_UnsetFunction(IShell* shell, const wchar_t* name);
ShellAlias* Shell_FindAlias(IShell* shell, const wchar_t* name);
bool Shell_SetAlias(IShell* shell, const wchar_t* name, const wchar_t* value);
bool Shell_UnsetAlias(IShell* shell, const wchar_t* name);
wchar_t* Shell_ExpandAliases(IShell* shell, const wchar_t* input);
bool Shell_AddLocal(IShell* shell, const wchar_t* name, const wchar_t* value);
bool CasePatternMatch(const wchar_t* pattern, const wchar_t* text);
void Shell_FireTrap(IShell* shell, const wchar_t* sig);
void Shell_FreeTraps(void);
void Shell_InstallSignalHandler(void);
void Shell_DispatchPendingSignals(IShell* shell);
extern volatile LONG g_interrupted;
extern HANDLE g_foreground_process;
extern IShell* g_active_shell;
bool SetShellOption(IShell* shell, const wchar_t* name, bool enabled);
void PrintShellOptions(IShell* shell, bool reusable);
bool IsValidIdentifier(const wchar_t* name);
void PrintShellTime(double seconds);
double FileTimeSeconds(const FILETIME* time);
ShellLimit* FindShellLimit(IShell* shell, wchar_t option);
void PrintShellLimit(const ShellLimit* limit, bool hard);
bool SameDirectory(const wchar_t* left, const wchar_t* right);
void PrintPosixPath(const wchar_t* path);
void Shell_LoadKshIni(IShell* shell);
void Shell_LoadKshrc(IShell* shell);
bool Shell_IsAdmin(void);
int Shell_RunSelfTests(IShell* shell);

/* History & Jobs */
extern wchar_t* g_history[MAX_HISTORY_ENTRIES];
extern int g_history_count;
void History_Add(const wchar_t* line);
void History_LoadFromFile(void);
void History_SaveToFile(const wchar_t* line);
void History_Cleanup(void);

extern ShellJob g_jobs[64];
extern int g_job_count;
void Shell_UpdateJobs(void);
int Shell_AddJob(DWORD pid, HANDLE hProcess, const wchar_t* command);
DWORD Shell_LastBackgroundPid(void);

/* Batch prototypes */
bool IsBatchFileName(const wchar_t* path);
int Shell_RunBatch(IShell* shell, const wchar_t* filename, int argumentCount, wchar_t* const arguments[]);
int Shell_RunBatchStage(IShell* shell, const wchar_t* scriptPath, const wchar_t* text, int argumentCount, wchar_t* const arguments[]);
bool Batch_EvaluateArithmetic(IShell* shell, BatchContext* context, const wchar_t* expression, long long* result);
wchar_t* Batch_Expand(IShell* shell, BatchContext* context, const wchar_t* input, int mode);
bool Batch_Append(wchar_t** output, size_t* length, size_t* capacity, const wchar_t* value, size_t valueLength);
BatchLoopVariable* Batch_FindLoopVariable(BatchContext* context, wchar_t name);
const wchar_t* Batch_GetVariable(IShell* shell, BatchContext* context, const wchar_t* name, wchar_t* scratch, size_t scratchCapacity);
bool Batch_ApplyModifiers(IShell* shell, const wchar_t* value, const wchar_t* modifiers, const wchar_t* searchVariable, wchar_t* output, size_t outputCapacity);
void Batch_SetLoopValue(BatchContext* context, BatchLoopVariable* variable, const wchar_t* value);
int Batch_ExecuteStatements(IShell* shell, BatchContext* context, const BatchFileLines* lines, const wchar_t* text);
int Batch_ExecuteExpanded(IShell* shell, BatchContext* context, const BatchFileLines* lines, const wchar_t* line);
bool Batch_FlowStopped(IShell* shell, BatchContext* context);
int Batch_RunLines(IShell* shell, BatchContext* context, const BatchFileLines* lines, size_t firstLine);
bool Batch_LoadLines(const wchar_t* filename, BatchFileLines* lines);
bool Batch_PushLine(BatchFileLines* lines, const wchar_t* text);
void Batch_FreeLines(BatchFileLines* lines);
bool Batch_EndLocal(IShell* shell, BatchContext* context);
bool Batch_SetVariable(IShell* shell, BatchContext* context, const wchar_t* name, const wchar_t* value);
bool Batch_RecordVariableChange(IShell* shell, BatchContext* context, const wchar_t* name);
bool Batch_IsCommand(const wchar_t* line, const wchar_t* name, const wchar_t** rest);
int Batch_FindLabel(const BatchFileLines* lines, const wchar_t* label, size_t startLine);
bool Batch_IsNumber(const wchar_t* text, long long* value);
const wchar_t* Batch_SkipSpaces(const wchar_t* text);
size_t Batch_FindBlockEnd(const wchar_t* text, size_t open);
bool Batch_ReadDelimitedToken(const wchar_t** cursor, wchar_t* item, size_t capacity);
int Batch_ExecuteSet(IShell* shell, BatchContext* context, const wchar_t* rest);
void Batch_StripQuotes(const wchar_t* source, wchar_t* target, size_t capacity);
int Batch_ExecuteIf(IShell* shell, BatchContext* context, const BatchFileLines* lines, const wchar_t* rest);
int Batch_ExecuteFor(IShell* shell, BatchContext* context, const BatchFileLines* lines, const wchar_t* rest);
int Batch_ExecuteCodePage(const wchar_t* rest);
int Batch_ExecuteColor(IShell* shell, const wchar_t* rest);
int Batch_ExecuteTitle(const wchar_t* rest);
int Batch_ExecuteVolume(const wchar_t* rest);
int Batch_ExecuteBreak(const wchar_t* rest);
int Batch_CopyMove(IShell* shell, const wchar_t* rest, bool move);
int Batch_DeleteFiles(const wchar_t* rest);
bool Batch_CreateDirectoryPath(const wchar_t* path);
bool Batch_IsProtectedDeleteTarget(const wchar_t* directory);
bool Batch_ConfirmRecursiveDelete(const wchar_t* directory);
bool Batch_DeleteTree(const wchar_t* directory);
bool Batch_ReadTextFile(const wchar_t* path, wchar_t** text);
wchar_t* Batch_DecodeText(const char* data, size_t size, bool allowBom);
HANDLE Batch_SpawnStage(BatchContext* context, const wchar_t* text, HANDLE input, HANDLE output, HANDLE error);
bool Batch_ReadWord(const wchar_t** cursor, wchar_t* word, size_t capacity);

#endif /* KSH_KSH_H */
