/* *************************************************************************
 *     gflashcard.c：抽認卡程序。
 *     版權 (C) 2024 gsm <406643764@qq.com>
 *     本程序為自由軟件：你可以依據自由軟件基金會所發布的第三版或更高版本的
 * GNU通用公共許可證重新發布、修改本程序。
 *     雖然基于使用目的而發布本程序，但不負任何擔保責任，亦不包含適銷性或特
 * 定目標之適用性的暗示性擔保。詳見GNU通用公共許可證。
 *     你應該已經收到一份附隨此程序的GNU通用公共許可證副本。否則，請參閱
 * <http://www.gnu.org/licenses/>。
 * ************************************************************************/

#include <stdio.h>
#include <stdlib.h>
#include <stdbool.h>
#include <string.h>
#include <ctype.h>
#include <signal.h>
#include <time.h>
#include <locale.h>
#include <libintl.h>

#define LINE_MAX 1024

/* 能形成長時記憶的最少連續答對次數。這個因人、問題復雜性而異 */
#define N_LONG_TERM_MEMORY 10

#define _(s) gettext(s)
#define die(...) do{fprintf(stderr, __VA_ARGS__); exit(EXIT_FAILURE);}while(0)

typedef struct flashcard_tag // 抽認卡記錄
{
    char *comment; // 注釋內容
    char *question; // 問題
    char *answer; // 標準答案
    int nquiz; // 復習次數，當爲表頭時則爲復習題數
    int n_contin_right; // 連續答對次數
    double right_rate; // 答題正確率（單位：%）
    time_t prev_time; // 上一次復習時間
    time_t next_time; // 下一次復習時間
    struct flashcard_tag *next; // 下一個抽認卡記錄
} Flashcard;

void set_locale(const char *program);
void usage(const char *program);
void set_signal(void);
Flashcard *load_flashcard(const char *filename);
FILE *Fopen(const char *filename, const char *mode);
Flashcard *create_flashcard(void);
void free_flashcards(Flashcard *list);
void free_flashcard(Flashcard *node);
void add_flashcard(Flashcard *node, Flashcard *list, time_t cur_time);
void del_flashcard(Flashcard *node, Flashcard *list);
void sort_flashcard(Flashcard *list);
bool has_flashcard(const Flashcard *list);
bool is_front_flashcard(const Flashcard *fc1, const Flashcard *fc2, bool cmp_time);
bool is_long_term_memory(const Flashcard *fc);
char *cat_string(char *dst, const char *src);
void load_info(Flashcard *fc, const char *input);
void fix_flashcard(Flashcard *fc);
void quiz(Flashcard *list);
void show_question(const char *question);
void input_question(void);
void show_answer(const char *answer);
bool judge_answer(void);
void eval_answer(Flashcard *fc, bool right);
void update_statistics(Flashcard *fc, bool right);
void show_statistics(const Flashcard *fc);
void exec_cmd(char *cmd);
char *trim_cmd(char *cmd);
void clear_screen(void);
void quit_for_signal(int signum);
void quit(void);
void update_data_file(const Flashcard *list, const char *data_file);
void show_template(void);
void help(void);

/* 以下使用全局變量的目的僅是爲了在程序異常退出時可以釋放資源 */
Flashcard *flashcards=NULL;
char *data_file=NULL;

int main(int argc, char **argv)
{
    set_locale(argv[0]);
    if(argc != 2)
        usage(argv[0]);
    data_file=argv[1];
    set_signal();
    flashcards=load_flashcard(data_file);
    quiz(flashcards);

    return EXIT_SUCCESS;
}

void set_locale(const char *program)
{
	if(!setlocale(LC_ALL, ""))
		fprintf(stderr, "warning: no locale support\n");
    else
    {
        bindtextdomain(program, "/usr/share/locale/");
        textdomain(program);
    }
}

void usage(const char *program)
{
    printf(_("用法：%s <數據文件名>\n"), program);
    puts(_("數據文件格式如下："));
    show_template();
    exit(EXIT_FAILURE);
} 

void set_signal(void)
{
	if(signal(SIGINT, quit_for_signal) == SIG_ERR)
        perror(_("不能安裝SIGINT信號處理函數"));
	if(signal(SIGTERM, quit_for_signal) == SIG_ERR)
        perror(_("不能安裝SIGTERM信號處理函數"));
}

Flashcard *load_flashcard(const char *filename)
{
    char line[LINE_MAX];
    enum { QUESTION, ANSWER, STATISTICS, IGNORE } stage=IGNORE;
    FILE *fp=Fopen(filename, "r");
    Flashcard *fc=NULL;
    Flashcard *list=create_flashcard();
    time_t cur_time=time(NULL);

    while(fgets(line, LINE_MAX, fp))
    {
        if(line[0]=='#' && !has_flashcard(list))
            list->comment=cat_string(list->comment, line);
        else if(line[0]=='>' && line[1]=='>')
            fc=create_flashcard(); 
        else if(line[0] == '#')
            fc->comment=cat_string(fc->comment, line);
        else if(line[0]=='Q' && line[1]==':')
            stage=QUESTION;
        else if(line[0]=='A' && line[1]==':')
            stage=ANSWER;
        else if(line[0]=='S' && line[1]==':')
            stage=STATISTICS;
        else if(line[0]=='<' && line[1]=='<')
            stage=IGNORE, fix_flashcard(fc), add_flashcard(fc, list, cur_time);
        else if(stage == QUESTION)
            fc->question=cat_string(fc->question, line);
        else if(stage == ANSWER)
            fc->answer=cat_string(fc->answer, line);
        else if(stage == STATISTICS)
            load_info(fc, line);
    }
    fclose(fp);

    return list;
}

FILE *Fopen(const char *filename, const char *mode)
{
    FILE *fp=fopen(filename, mode);
    if(fp == NULL)
        die(_("打開文件失敗：%s\n"), filename);
    return fp;
}

Flashcard *create_flashcard(void)
{
    Flashcard *fc=malloc(sizeof(Flashcard));
    fc->comment=fc->question=fc->answer=NULL;
    fc->nquiz=fc->n_contin_right=0;
    fc->right_rate=0.0;
    fc->prev_time=fc->next_time=0;
    fc->next=NULL;

    return fc;
}

void free_flashcards(Flashcard *list)
{
    for(Flashcard *p=list->next, *next=NULL; p; p=next)
    {
        next=p->next;
        del_flashcard(p, list);
        free_flashcard(p);
    }
    free_flashcard(list);
}

void free_flashcard(Flashcard *node)
{
    free(node->comment);
    free(node->question);
    free(node->answer);
    free(node);
}

void add_flashcard(Flashcard *node, Flashcard *list, time_t cur_time)
{
    Flashcard *p=NULL, *prev=NULL;
    bool cmp_time = (cur_time > node->next_time);

    for(p=list->next, prev=list; p; prev=p, p=p->next)
        if(is_front_flashcard(node, p, cmp_time))
            break;

    prev->next=node, node->next=p;
}

void del_flashcard(Flashcard *node, Flashcard *list)
{
    for(Flashcard *p=list->next, *prev=list; p; prev=p, p=p->next)
        if(p == node)
            { prev->next=p->next; return; }
}

void sort_flashcard(Flashcard *list)
{
    if(!has_flashcard(list))
        return;

    time_t cur_time=time(NULL);
    Flashcard *next=NULL, *head=create_flashcard();
    for(Flashcard *p=list->next; p; p=next)
    {
        next=p->next;
        del_flashcard(p, list);
        add_flashcard(p, head, cur_time);
    }
    list->next=head->next;
    free_flashcard(head);
}

bool has_flashcard(const Flashcard *list)
{
    return list && list->next;
}

bool is_front_flashcard(const Flashcard *fc1, const Flashcard *fc2, bool cmp_time)
{
    return (cmp_time && (fc1->next_time < fc2->next_time))
        || (!is_long_term_memory(fc1) && !is_long_term_memory(fc2)
            && fc1->n_contin_right < fc2->n_contin_right)
        || fc1->right_rate < fc2->right_rate;
}

bool is_long_term_memory(const Flashcard *fc)
{
    return (fc->n_contin_right >= N_LONG_TERM_MEMORY);
}

char *cat_string(char *dst, const char *src)
{
    if(dst)
        dst=realloc(dst, strlen(dst)+strlen(src)+1);
    else
        dst=malloc(strlen(src)+1), dst[0]='\0';
    return strcat(dst, src);
}

void load_info(Flashcard *fc, const char *input)
{
    sscanf(input, "%d %d %lf", &fc->nquiz, &fc->n_contin_right, &fc->right_rate);
    fc->prev_time=time(NULL);
}

void fix_flashcard(Flashcard *fc)
{
    if(fc->comment == NULL)
        fc->comment=cat_string(NULL, "");
    if(fc->question == NULL)
        fc->question=cat_string(NULL, "\n");
    if(fc->answer == NULL)
        fc->answer=cat_string(NULL, "\n");

    if(fc->prev_time == 0)
        fc->prev_time=time(NULL);

    struct tm *p=gmtime(&fc->prev_time);
    if(p->tm_mon < 11)
        p->tm_mon++;
    else
        p->tm_mon=0, p->tm_year++;
    fc->next_time=mktime(p);
}

void quiz(Flashcard *list)
{
    if(!has_flashcard(list))
    {
        free_flashcard(list);
        die(_("數據文件不包含有效的抽認卡記錄！\n"));
    }

    bool right;
    for(Flashcard *p=list->next; p; p=p->next)
    {
        if(is_long_term_memory(p))
            continue;
        show_question(p->question);
        input_question();
        show_answer(p->answer);
        right=judge_answer();
        eval_answer(p, right);
        update_statistics(list, right);
    }
    puts(_("復習完成。"));
    show_statistics(list);
}

void show_quiz_result(const Flashcard *list)
{
    for(Flashcard *p=list->next; p; p=p->next)
    {
        if(is_long_term_memory(p))
            continue;
    }
}

void show_question(const char *question)
{
    puts(_("問題："));
    printf("%s", question);
    puts(_("請輸入答案（以獨立一行<<<結束）："));
}

void input_question(void)
{
    char line[LINE_MAX];
    while(fgets(line, LINE_MAX, stdin))
    {
        if(strcmp(line, "<<<\n") == 0)
            return;
        exec_cmd(line);
    }
}

void show_answer(const char *answer)
{
    puts(_("答案："));
    printf("%s", answer);
}

bool judge_answer(void)
{
    char line[LINE_MAX];
    while(1)
    {
        puts(_("是否正確？(正確按y/錯誤按n）"));
        fgets(line, LINE_MAX, stdin);
        exec_cmd(line);
        if(strcmp(line, "y\n") == 0)
            return true;
        if(strcmp(line, "n\n") == 0)
            return false;
    }
}

void eval_answer(Flashcard *fc, bool right)
{
    update_statistics(fc, right);
    show_statistics(fc);
}

void update_statistics(Flashcard *fc, bool right)
{
    double rate=fc->right_rate, n=fc->nquiz;

    fc->nquiz++;
    fc->n_contin_right = right ? fc->n_contin_right+1 : 0;

    if(right)
        fc->right_rate=(rate*n+100)/(n+1);
    else if(rate > 0)
        fc->right_rate=(rate*n-100)/(n+1);
    else
        fc->right_rate=0;
}

void show_statistics(const Flashcard *fc)
{
    bool is_head=fc->question;

    if(is_head)
        printf(_("共復習%d題，正確率爲%.0lf%%。\n\n"),
            fc->nquiz, fc->right_rate);
    else
        printf(_("共復習%d次，連續答對%d次，正確率爲%.0lf%%。\n\n"),
            fc->nquiz, fc->n_contin_right, fc->right_rate);
}

void exec_cmd(char *cmd)
{
    trim_cmd(cmd);

    if(strcmp(cmd, "help\n") == 0)
        help();
    else if(strcmp(cmd, "quit\n") == 0)
        quit();
    else if(strcmp(cmd, "temp\n") == 0)
        show_template();
    else if(strcmp(cmd, "clear\n") == 0)
        clear_screen();
}

char *trim_cmd(char *cmd)
{
    for(char *p=cmd; p && *p; p++)
        if(isgraph(*p))
            { memmove(cmd, p, strlen(p)+1); break; }

    for(int i=strlen(cmd)-2; i>=0 ; i--)
        if(isgraph(cmd[i]))
            { cmd[i+1]='\n'; cmd[i+2]='\0'; break; }

    return cmd;
}

void clear_screen(void)
{
#ifdef ANSI_ESCAPE
    puts("[2J");
#else
    for(int i=0; i<100; i++)
        puts("");
#endif
}

void quit_for_signal(int signum)
{
    (void)signum;
    quit();
}

void quit(void)
{
    sort_flashcard(flashcards);
    update_data_file(flashcards, data_file);
    free_flashcards(flashcards);
    exit(EXIT_SUCCESS);
}

void update_data_file(const Flashcard *list, const char *data_file)
{
    FILE *fp=Fopen(data_file, "w");

    fputs(list->comment, fp);
    for(Flashcard *p=list->next; p; p=p->next)
    {
        fputs("\n", fp);
        fputs(">>\n", fp);
        fputs(p->comment, fp);
        fputs("Q:\n", fp);
        fputs(p->question, fp);
        fputs("A:\n", fp);
        fputs(p->answer, fp);
        fputs("S:\n", fp);
        fprintf(fp, "    %d %d %g %lu %lu\n", p->nquiz, p->n_contin_right,
            p->right_rate, p->prev_time, p->next_time);
        fputs("<<\n", fp);
    }

    fclose(fp);
}

void show_template(void)
{
    puts(_("# XXX抽認卡記錄表"));
    puts(_("# 本文件由注釋(以#開頭的行均視爲注釋)、抽認卡記錄、空白行組成。"));
    puts(_("# 抽認卡記錄由記錄開始標記(>>)、注釋、空白行、問題開始標記(Q:)、"));
    puts(_("# 問題、答案開始標記(A:)、答案、統計信息、記錄結束標記(<<)組成。"));
    puts(_("# 其中注釋、空白行是可選的。注釋以#開頭。問題、答案、統計信息"));
    puts(_("# 以制表符開頭。統計信息依次表示復習次數、連續答對次數、正確率、"));
    puts(_("# 經編碼的上次復習時間和下次復習時間。各統計信息項是可選的，但"));
    puts(_("# 只能由後向前依次省略，其中後兩者不應手動錄入。程序更新本表時"));
    puts(_("# 會有選擇地保留注釋，包括：頭部注釋、抽認卡記錄內部注釋。"));
    puts("");
    puts(">>");
    puts("[# 注釋]");
    puts("Q:");
    puts(_("    具體問題"));
    puts("A:");
    puts(_("    具體答案"));
    puts("S:");
    puts(_("    [復習次數] [連續答對次數] [正確率] [上次復習時間] [下次復習時間]"));
    puts("<<");
    puts("");
    puts(_("[其他抽認卡記錄]"));
}

void help(void)
{
    puts(_("    在任何時刻，執行help命令均能顯示本幫助信息。"));
    puts(_("執行某個命令的意思是輸入命令名並按Enter鍵。"));
    puts(_("目前支持的命令有："));
    puts(_("    help      顯示本幫助信息。"));
    puts(_("    quit      退出本程序。"));
    puts(_("    temp      顯示數據文件模板。"));
    puts(_("    clear     清屏。"));
}
