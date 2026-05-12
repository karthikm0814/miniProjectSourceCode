/*
 * Transaction Processing System  –  CS3251 Lab Mini Project
 *
 * Improvements over original:
 *   - snake_case throughout (was camelCase)
 *   - Functional decomposition: I/O separated from logic
 *   - Memory: stack-only, no malloc; removed double-seek bug
 *   - Speed: fread==1 loop instead of feof; fflush after writes
 *   - New: list accounts, ATM debit, search by name, sort by balance
 *   - Error handling on every scanf/fread/fwrite
 *   - Balance floor (MIN_BALANCE) and ATM daily limit
 *   - Delete requires y/n confirmation
 *   - Auto-creates credit.dat on first run
 *   - Account numbers are 15-digit sequences (string-based)
 *   - Transaction history / audit log with timestamps
 *
 * Compile:  gcc trans.c -o trans
 * Run:      ./trans
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

/* Directory where the executable lives (filled in main) */
static char exe_dir[512] = ".";

/* Build a full path: exe_dir + filename -> dest */
static void build_path(char *dest, size_t dest_sz, const char *filename)
{
    snprintf(dest, dest_sz, "%s/%s", exe_dir, filename);
}

#define MAX_ACCOUNTS    100
#define MAX_LOG_DISPLAY 500  /* max log entries to show at once */
#define ACCT_NUM_LEN    16   /* 15 digits + null terminator */
#define MIN_BALANCE     -500.0
#define ATM_DAILY_LIMIT 10000.0
#define DATA_FILE       "credit.dat"
#define TEXT_FILE       "accounts.txt"
#define CSV_FILE        "accounts.csv"
#define LOG_FILE        "trans_log.dat"
#define LOG_CSV_FILE    "trans_log.csv"

typedef struct {
    char   acct_num[ACCT_NUM_LEN];  /* 15-digit account number string */
    char   last_name[15];
    char   first_name[10];
    double balance;
} client_data_t;

/* Transaction types for audit log */
#define TXN_CREDIT       1   /* charge / deposit via update_record */
#define TXN_PAYMENT      2   /* payment via update_record */
#define TXN_ATM_DEBIT    3   /* ATM withdrawal */
#define TXN_ACCT_OPEN    4   /* new account created (initial balance) */
#define TXN_ACCT_CLOSE   5   /* account deleted */
#define TXN_TRANSFER     6   /* transfer between accounts */

typedef struct {
    char   acct_num[ACCT_NUM_LEN];  /* which account */
    int    txn_type;                /* TXN_CREDIT, TXN_PAYMENT, etc. */
    double amount;                  /* transaction amount (always positive) */
    double balance_after;           /* balance after this transaction */
    time_t timestamp;               /* when it happened */
} transaction_log_t;

/* ---------- prototypes ---------- */
unsigned int  enter_choice(void);
int           read_account_number(const char *prompt, char *out_acct);
double        read_amount(const char *prompt);
void          generate_account_number(char *out_acct);
int           find_record(FILE *fp, const char *acct_num, client_data_t *out, long *pos);
int           save_record_at(FILE *fp, long pos, const client_data_t *rec);
int           append_record(FILE *fp, const client_data_t *rec);
int           load_all_records(FILE *fp, client_data_t *records, int max_count);
void          rewrite_all_records(FILE *fp, client_data_t *records, int count);
void          print_header(FILE *out);
void          print_record(FILE *out, const client_data_t *c);
static int    apply_transaction(client_data_t *client, double amount);
void          text_file(FILE *fp);
void          list_accounts(FILE *fp);
void          update_record(FILE *fp);
void          atm_debit(FILE *fp);
void          new_record(FILE *fp);
void          delete_record(FILE *fp);
void          search_by_name(FILE *fp);
void          sort_and_display(FILE *fp);
void          csv_export(FILE *fp);
void          transfer_between_accounts(FILE *fp);
void          log_transaction(const char *acct_num, int txn_type,
                              double amount, double balance_after);
const char   *txn_type_str(int txn_type);
void          view_history(FILE *fp);
void          export_history_csv(void);
void          create_blank_file(void);

/* ---------- main ---------- */
int main(int argc, char *argv[])
{
    /* Determine directory of this executable */
    {
        char *last_sep = NULL;
        char *p;
        strncpy(exe_dir, argv[0], sizeof(exe_dir) - 1);
        exe_dir[sizeof(exe_dir) - 1] = '\0';
        for (p = exe_dir; *p; p++) {
            if (*p == '/' || *p == '\\') last_sep = p;
        }
        if (last_sep) *last_sep = '\0';
        else          strcpy(exe_dir, ".");
    }
    FILE        *cf_ptr;
    unsigned int choice;
    char         path_buf[512];

    srand((unsigned)time(NULL));

    build_path(path_buf, sizeof(path_buf), DATA_FILE);
    cf_ptr = fopen(path_buf, "rb+");
    if (cf_ptr == NULL) {
        printf("'%s' not found. Creating blank file...\n", path_buf);
        create_blank_file();
        cf_ptr = fopen(path_buf, "rb+");
        if (cf_ptr == NULL) {
            fprintf(stderr, "Fatal: cannot open '%s'.\n", path_buf);
            return EXIT_FAILURE;
        }
    }

    while ((choice = enter_choice()) != 13) {
        switch (choice) {
        case  1: text_file(cf_ptr);        break;
        case  2: list_accounts(cf_ptr);    break;
        case  3: update_record(cf_ptr);    break;
        case  4: atm_debit(cf_ptr);        break;
        case  5: new_record(cf_ptr);       break;
        case  6: delete_record(cf_ptr);    break;
        case  7: search_by_name(cf_ptr);   break;
        case  8: sort_and_display(cf_ptr); break;
        case  9: csv_export(cf_ptr);       break;
        case 10: view_history(cf_ptr);     break;
        case 11: export_history_csv();     break;
        case 12: transfer_between_accounts(cf_ptr); break;
        default: puts("Invalid choice. Enter 1-13."); break;
        }
    }

    fclose(cf_ptr);
    puts("Goodbye!");
    return EXIT_SUCCESS;
}

/* ---------- menu ---------- */
unsigned int enter_choice(void)
{
    unsigned int choice;
    printf("\n===== Transaction Processing System =====\n"
           " 1 - Export accounts to '%s'\n"
           " 2 - List all accounts (screen)\n"
           " 3 - Update account balance\n"
           " 4 - ATM debit transaction\n"
           " 5 - Add new account\n"
           " 6 - Delete account\n"
           " 7 - Search by last name\n"
           " 8 - Sort and display by balance\n"
           " 9 - Export accounts to CSV\n"
           "10 - View transaction history\n"
           "11 - Export transaction log to CSV\n"
           "12 - Transfer between accounts\n"
           "13 - Exit\n? ", TEXT_FILE);
    if (scanf("%u", &choice) != 1) {
        int c; while ((c = getchar()) != '\n' && c != EOF);
        return 0;
    }
    return choice;
}

/* ---------- I/O helpers (user-facing) ---------- */

/* Read a 15-digit account number from user. Returns 1 on success, -1 on error. */
int read_account_number(const char *prompt, char *out_acct)
{
    char buf[64];
    int i;

    printf("%s", prompt);
    if (scanf("%63s", buf) != 1) {
        int c; while ((c = getchar()) != '\n' && c != EOF);
        puts("Invalid input.");
        return -1;
    }

    /* Validate: must be exactly 15 digits */
    if (strlen(buf) != 15) {
        printf("Invalid: account number must be exactly 15 digits.\n");
        return -1;
    }
    for (i = 0; i < 15; i++) {
        if (buf[i] < '0' || buf[i] > '9') {
            printf("Invalid: account number must contain only digits.\n");
            return -1;
        }
    }

    strncpy(out_acct, buf, ACCT_NUM_LEN);
    out_acct[ACCT_NUM_LEN - 1] = '\0';
    return 1;
}

/* Generate a random 15-digit account number */
void generate_account_number(char *out_acct)
{
    int i;
    /* First digit: 1-9 to avoid leading zero */
    out_acct[0] = '1' + (rand() % 9);
    for (i = 1; i < 15; i++)
        out_acct[i] = '0' + (rand() % 10);
    out_acct[15] = '\0';
}

double read_amount(const char *prompt)
{
    double amount;
    printf("%s", prompt);
    if (scanf("%lf", &amount) != 1) {
        int c; while ((c = getchar()) != '\n' && c != EOF);
        puts("Invalid amount.");
        return 0.0;
    }
    return amount;
}

/* ---------- file helpers ---------- */

/* Search for an account by number. Returns 1 if found, 0 if not, -1 on error.
   If found, *out is filled and *pos is set to the file offset of the record. */
int find_record(FILE *fp, const char *acct_num, client_data_t *out, long *pos)
{
    client_data_t client;

    rewind(fp);
    while (fread(&client, sizeof(client_data_t), 1, fp) == 1) {
        if (strcmp(client.acct_num, acct_num) == 0) {
            if (out) *out = client;
            if (pos) *pos = ftell(fp) - (long)sizeof(client_data_t);
            return 1;
        }
    }
    return 0;
}

/* Save a record at a specific file position */
int save_record_at(FILE *fp, long pos, const client_data_t *rec)
{
    if (fseek(fp, pos, SEEK_SET) != 0) return -1;
    if (fwrite(rec, sizeof(client_data_t), 1, fp) != 1) return -1;
    fflush(fp);
    return 1;
}

/* Append a record to end of file */
int append_record(FILE *fp, const client_data_t *rec)
{
    if (fseek(fp, 0L, SEEK_END) != 0) return -1;
    if (fwrite(rec, sizeof(client_data_t), 1, fp) != 1) return -1;
    fflush(fp);
    return 1;
}

/* Load all active (non-empty acct_num) records into array. Returns count. */
int load_all_records(FILE *fp, client_data_t *records, int max_count)
{
    client_data_t client;
    int count = 0;

    rewind(fp);
    while (fread(&client, sizeof(client_data_t), 1, fp) == 1) {
        if (client.acct_num[0] != '\0' && count < max_count) {
            records[count++] = client;
        }
    }
    return count;
}

/* Rewrite file with only the given records (used after delete) */
void rewrite_all_records(FILE *fp, client_data_t *records, int count)
{
    int i;

    /* Close and reopen to truncate */
    char rpath[512];
    build_path(rpath, sizeof(rpath), DATA_FILE);
    freopen(rpath, "wb+", fp);
    for (i = 0; i < count; i++)
        fwrite(&records[i], sizeof(client_data_t), 1, fp);
    fflush(fp);

    /* Reopen in rb+ mode for normal operations */
    freopen(rpath, "rb+", fp);
}

/* ---------- display helpers ---------- */
void print_header(FILE *out)
{
    fprintf(out, "%-17s %-15s %-10s %12s\n",
            "Account Number", "Last Name", "First Name", "Balance");
    fprintf(out, "%-17s %-15s %-10s %12s\n",
            "-----------------", "---------------", "----------", "------------");
}

void print_record(FILE *out, const client_data_t *c)
{
    fprintf(out, "%-17s %-15s %-10s %12.2f\n",
            c->acct_num, c->last_name, c->first_name, c->balance);
}

/* ---------- apply_transaction (pure logic, no I/O) ---------- */
static int apply_transaction(client_data_t *client, double amount)
{
    double new_balance = client->balance + amount;
    if (new_balance < MIN_BALANCE) {
        printf("Denied: balance would be %.2f (minimum: %.2f).\n",
               new_balance, MIN_BALANCE);
        return 0;
    }
    client->balance = new_balance;
    return 1;
}

/* ---------- menu actions ---------- */
void text_file(FILE *fp)
{
    FILE          *out;
    client_data_t  client;
    int            count = 0;

    char tpath[512];
    build_path(tpath, sizeof(tpath), TEXT_FILE);
    out = fopen(tpath, "w");
    if (!out) { perror(tpath); return; }

    rewind(fp);
    print_header(out);
    while (fread(&client, sizeof(client_data_t), 1, fp) == 1)
        if (client.acct_num[0] != '\0') { print_record(out, &client); count++; }

    fprintf(out, "\nTotal active accounts: %d\n", count);
    fclose(out);
    printf("Exported %d account(s) to '%s'.\n", count, tpath);
}

void list_accounts(FILE *fp)
{
    client_data_t client;
    int           count = 0;

    rewind(fp);
    print_header(stdout);
    while (fread(&client, sizeof(client_data_t), 1, fp) == 1)
        if (client.acct_num[0] != '\0') { print_record(stdout, &client); count++; }

    if (count == 0) puts("No accounts found.");
    else printf("\nTotal: %d account(s).\n", count);
}

void update_record(FILE *fp)
{
    char          acct[ACCT_NUM_LEN];
    double        transaction;
    client_data_t client;
    long          pos;

    if (read_account_number("Enter 15-digit account number to update: ", acct) < 0)
        return;

    switch (find_record(fp, acct, &client, &pos)) {
    case -1: puts("File read error."); return;
    case  0: printf("Account %s has no information.\n", acct); return;
    }

    puts("\nCurrent details:"); print_header(stdout); print_record(stdout, &client);
    transaction = read_amount("\nEnter charge (+) or payment (-): ");

    if (apply_transaction(&client, transaction)) {
        if (save_record_at(fp, pos, &client) < 0)
            { puts("File write error."); return; }
        log_transaction(client.acct_num,
                        transaction >= 0 ? TXN_CREDIT : TXN_PAYMENT,
                        transaction >= 0 ? transaction : -transaction,
                        client.balance);
        puts("\nUpdated record:"); print_header(stdout); print_record(stdout, &client);
    }
}

void atm_debit(FILE *fp)
{
    char          acct[ACCT_NUM_LEN];
    double        amount;
    client_data_t client;
    long          pos;

    if (read_account_number("Enter 15-digit account number for ATM debit: ", acct) < 0)
        return;

    switch (find_record(fp, acct, &client, &pos)) {
    case -1: puts("File read error."); return;
    case  0: printf("Account %s has no information.\n", acct); return;
    }

    amount = read_amount("Enter ATM debit amount: ");
    if (amount <= 0.0) { puts("Amount must be positive."); return; }
    if (amount > ATM_DAILY_LIMIT) {
        printf("Denied: exceeds ATM daily limit of %.2f.\n", ATM_DAILY_LIMIT);
        return;
    }

    if (apply_transaction(&client, -amount)) {
        if (save_record_at(fp, pos, &client) < 0)
            { puts("File write error."); return; }
        log_transaction(client.acct_num, TXN_ATM_DEBIT, amount, client.balance);
        printf("ATM debit of %.2f applied.\n", amount);
        print_header(stdout); print_record(stdout, &client);
    }
}

void new_record(FILE *fp)
{
    client_data_t client;
    client_data_t dummy;
    long          dummy_pos;
    char          choice_buf[4];
    int           count;

    memset(&client, 0, sizeof(client));

    /* Check we haven't hit the max */
    {
        client_data_t temp;
        count = 0;
        rewind(fp);
        while (fread(&temp, sizeof(client_data_t), 1, fp) == 1)
            if (temp.acct_num[0] != '\0') count++;
        if (count >= MAX_ACCOUNTS) {
            printf("Maximum number of accounts (%d) reached.\n", MAX_ACCOUNTS);
            return;
        }
    }

    /* Let user choose: auto-generate or enter manually */
    printf("Generate account number automatically? (y/n): ");
    scanf("%3s", choice_buf);

    if (choice_buf[0] == 'y' || choice_buf[0] == 'Y') {
        /* Auto-generate a unique 15-digit number */
        do {
            generate_account_number(client.acct_num);
        } while (find_record(fp, client.acct_num, &dummy, &dummy_pos) == 1);
        printf("Generated account number: %s\n", client.acct_num);
    } else {
        if (read_account_number("Enter 15-digit account number: ", client.acct_num) < 0)
            return;
        if (find_record(fp, client.acct_num, &dummy, &dummy_pos) == 1) {
            printf("Account %s already exists.\n", client.acct_num);
            return;
        }
    }

    printf("Enter last name, first name, initial balance:\n? ");
    if (scanf("%14s%9s%lf",
              client.last_name, client.first_name, &client.balance) != 3) {
        puts("Invalid input. Account not created."); return;
    }
    if (client.balance < MIN_BALANCE) {
        printf("Initial balance %.2f below minimum %.2f.\n",
               client.balance, MIN_BALANCE); return;
    }

    if (append_record(fp, &client) < 0)
        { puts("File write error."); return; }
    log_transaction(client.acct_num, TXN_ACCT_OPEN, client.balance, client.balance);
    printf("Account %s created.\n", client.acct_num);
    print_header(stdout); print_record(stdout, &client);
}

void delete_record(FILE *fp)
{
    client_data_t client;
    client_data_t records[MAX_ACCOUNTS];
    char          acct[ACCT_NUM_LEN];
    char          confirm;
    long          pos;
    int           count, i, new_count;

    if (read_account_number("Enter 15-digit account number to delete: ", acct) < 0)
        return;

    switch (find_record(fp, acct, &client, &pos)) {
    case -1: puts("File read error."); return;
    case  0: printf("Account %s does not exist.\n", acct); return;
    }

    print_header(stdout); print_record(stdout, &client);
    printf("Confirm delete account %s? (y/n): ", acct);
    scanf(" %c", &confirm);
    if (confirm != 'y' && confirm != 'Y') { puts("Deletion cancelled."); return; }

    /* Load all records except the one to delete, then rewrite */
    count = load_all_records(fp, records, MAX_ACCOUNTS);
    new_count = 0;
    for (i = 0; i < count; i++) {
        if (strcmp(records[i].acct_num, acct) != 0)
            records[new_count++] = records[i];
    }
    rewrite_all_records(fp, records, new_count);
    log_transaction(acct, TXN_ACCT_CLOSE, client.balance, 0.0);
    printf("Account %s deleted.\n", acct);
}

void search_by_name(FILE *fp)
{
    char          search[15];
    client_data_t client;
    int           found = 0;

    printf("Enter last name to search: ");
    scanf("%14s", search);

    rewind(fp);
    while (fread(&client, sizeof(client_data_t), 1, fp) == 1) {
        if (client.acct_num[0] != '\0' &&
            strncasecmp(client.last_name, search, 14) == 0) {
            if (!found) { puts("\nMatching accounts:"); print_header(stdout); }
            print_record(stdout, &client);
            found++;
        }
    }
    if (!found) printf("No account found with last name '%s'.\n", search);
    else        printf("\n%d match(es) found.\n", found);
}

void sort_and_display(FILE *fp)
{
    client_data_t records[MAX_ACCOUNTS];
    int           count, i, j;
    client_data_t key;

    count = load_all_records(fp, records, MAX_ACCOUNTS);

    if (count == 0) { puts("No accounts to display."); return; }

    /* Insertion sort — descending by balance */
    for (i = 1; i < count; i++) {
        key = records[i]; j = i - 1;
        while (j >= 0 && records[j].balance < key.balance)
            { records[j + 1] = records[j]; j--; }
        records[j + 1] = key;
    }

    puts("\nAccounts sorted by balance (highest first):");
    print_header(stdout);
    for (i = 0; i < count; i++) print_record(stdout, &records[i]);
    printf("\nTotal: %d account(s).\n", count);
}

/* ---------- CSV export ---------- */
void csv_export(FILE *fp)
{
    FILE          *out;
    client_data_t  client;
    int            count = 0;
    double         total_balance = 0.0;

    char cpath[512];
    build_path(cpath, sizeof(cpath), CSV_FILE);
    out = fopen(cpath, "w");
    if (!out) { perror(cpath); return; }

    /* CSV header row */
    fprintf(out, "Account Number,Last Name,First Name,Balance\n");

    rewind(fp);
    while (fread(&client, sizeof(client_data_t), 1, fp) == 1) {
        if (client.acct_num[0] != '\0') {
            fprintf(out, "%s,%s,%s,%.2f\n",
                    client.acct_num,
                    client.last_name,
                    client.first_name,
                    client.balance);
            total_balance += client.balance;
            count++;
        }
    }

    /* Summary row */
    fprintf(out, "\nTotal Accounts,%d,,\n", count);
    fprintf(out, "Total Balance,,,%.2f\n", total_balance);

    fclose(out);
    printf("Exported %d account(s) to '%s'.\n", count, cpath);
    printf("Total balance across all accounts: %.2f\n", total_balance);
}

/* ---------- transfer between accounts ---------- */
void transfer_between_accounts(FILE *fp)
{
    char          from_acct[ACCT_NUM_LEN];
    char          to_acct[ACCT_NUM_LEN];
    double        amount;
    client_data_t from_client, to_client;
    long          from_pos, to_pos;

    /* Read source account */
    if (read_account_number("Enter sender's 15-digit account number: ", from_acct) < 0)
        return;

    switch (find_record(fp, from_acct, &from_client, &from_pos)) {
    case -1: puts("File read error."); return;
    case  0: printf("Sender account %s not found.\n", from_acct); return;
    }

    /* Read destination account */
    if (read_account_number("Enter receiver's 15-digit account number: ", to_acct) < 0)
        return;

    if (strcmp(from_acct, to_acct) == 0) {
        puts("Cannot transfer to the same account.");
        return;
    }

    switch (find_record(fp, to_acct, &to_client, &to_pos)) {
    case -1: puts("File read error."); return;
    case  0: printf("Receiver account %s not found.\n", to_acct); return;
    }

    /* Show both accounts */
    puts("\nSender account:");
    print_header(stdout); print_record(stdout, &from_client);
    puts("\nReceiver account:");
    print_header(stdout); print_record(stdout, &to_client);

    /* Read transfer amount */
    amount = read_amount("\nEnter amount to transfer: ");
    if (amount <= 0.0) { puts("Amount must be positive."); return; }

    /* Check sender has sufficient balance */
    if (!apply_transaction(&from_client, -amount)) {
        /* apply_transaction already prints the denial message */
        return;
    }

    /* Credit receiver (always succeeds for positive amount) */
    to_client.balance += amount;

    /* Save both records */
    if (save_record_at(fp, from_pos, &from_client) < 0) {
        puts("File write error (sender). Transfer aborted.");
        return;
    }
    if (save_record_at(fp, to_pos, &to_client) < 0) {
        /* Rollback sender — restore original balance */
        from_client.balance += amount;
        save_record_at(fp, from_pos, &from_client);
        puts("File write error (receiver). Transfer rolled back.");
        return;
    }

    /* Log both sides */
    log_transaction(from_acct, TXN_TRANSFER, amount, from_client.balance);
    log_transaction(to_acct,  TXN_TRANSFER, amount, to_client.balance);

    printf("\nTransfer of %.2f completed successfully!\n", amount);
    puts("\nUpdated sender:");
    print_header(stdout); print_record(stdout, &from_client);
    puts("\nUpdated receiver:");
    print_header(stdout); print_record(stdout, &to_client);
}

/* ---------- transaction history / audit log ---------- */

const char *txn_type_str(int txn_type)
{
    switch (txn_type) {
    case TXN_CREDIT:     return "Credit/Deposit";
    case TXN_PAYMENT:    return "Payment/Withdrawal";
    case TXN_ATM_DEBIT:  return "ATM Debit";
    case TXN_ACCT_OPEN:  return "Account Opened";
    case TXN_ACCT_CLOSE: return "Account Closed";
    case TXN_TRANSFER:   return "Transfer";
    default:             return "Unknown";
    }
}

/* Append one log entry to the transaction log file */
void log_transaction(const char *acct_num, int txn_type,
                     double amount, double balance_after)
{
    FILE *log_fp;
    transaction_log_t entry;
    char lpath[512];

    memset(&entry, 0, sizeof(entry));
    strncpy(entry.acct_num, acct_num, ACCT_NUM_LEN - 1);
    entry.acct_num[ACCT_NUM_LEN - 1] = '\0';
    entry.txn_type      = txn_type;
    entry.amount         = amount;
    entry.balance_after  = balance_after;
    entry.timestamp      = time(NULL);

    build_path(lpath, sizeof(lpath), LOG_FILE);
    log_fp = fopen(lpath, "ab");
    if (!log_fp) { perror(lpath); return; }
    fwrite(&entry, sizeof(transaction_log_t), 1, log_fp);
    fclose(log_fp);
}

/* View transaction history for a specific account */
void view_history(FILE *fp)
{
    char               acct[ACCT_NUM_LEN];
    client_data_t      client;
    long               dummy_pos;
    FILE              *log_fp;
    transaction_log_t  entry;
    int                found = 0;
    char               time_buf[26];
    char               lpath[512];

    if (read_account_number("Enter 15-digit account number: ", acct) < 0)
        return;

    /* Check if account exists (or existed) */
    if (find_record(fp, acct, &client, &dummy_pos) == 1) {
        puts("\nAccount details:");
        print_header(stdout);
        print_record(stdout, &client);
    } else {
        printf("Note: Account %s not currently active (may have been deleted).\n", acct);
    }

    build_path(lpath, sizeof(lpath), LOG_FILE);
    log_fp = fopen(lpath, "rb");
    if (!log_fp) {
        puts("No transaction history found.");
        return;
    }

    printf("\n%-22s %-20s %12s %14s\n",
           "Date/Time", "Type", "Amount", "Balance After");
    printf("%-22s %-20s %12s %14s\n",
           "----------------------", "--------------------",
           "------------", "--------------");

    while (fread(&entry, sizeof(transaction_log_t), 1, log_fp) == 1) {
        if (strcmp(entry.acct_num, acct) == 0) {
            struct tm *tm_info = localtime(&entry.timestamp);
            strftime(time_buf, sizeof(time_buf), "%Y-%m-%d %H:%M:%S", tm_info);
            printf("%-22s %-20s %12.2f %14.2f\n",
                   time_buf,
                   txn_type_str(entry.txn_type),
                   entry.amount,
                   entry.balance_after);
            found++;
        }
    }

    fclose(log_fp);
    if (found == 0)
        printf("No transaction history for account %s.\n", acct);
    else
        printf("\n%d transaction(s) found.\n", found);
}

/* Export the entire transaction log to CSV */
void export_history_csv(void)
{
    FILE              *log_fp, *out;
    transaction_log_t  entry;
    int                count = 0;
    char               time_buf[26];
    char               lpath[512], cpath[512];

    build_path(lpath, sizeof(lpath), LOG_FILE);
    log_fp = fopen(lpath, "rb");
    if (!log_fp) {
        puts("No transaction history found.");
        return;
    }

    build_path(cpath, sizeof(cpath), LOG_CSV_FILE);
    out = fopen(cpath, "w");
    if (!out) { perror(cpath); fclose(log_fp); return; }

    /* CSV header */
    fprintf(out, "Date/Time,Account Number,Transaction Type,Amount,Balance After\n");

    while (fread(&entry, sizeof(transaction_log_t), 1, log_fp) == 1) {
        struct tm *tm_info = localtime(&entry.timestamp);
        strftime(time_buf, sizeof(time_buf), "%Y-%m-%d %H:%M:%S", tm_info);
        fprintf(out, "%s,%s,%s,%.2f,%.2f\n",
                time_buf,
                entry.acct_num,
                txn_type_str(entry.txn_type),
                entry.amount,
                entry.balance_after);
        count++;
    }

    fclose(log_fp);
    fclose(out);
    printf("Exported %d transaction(s) to '%s'.\n", count, cpath);
}

void create_blank_file(void)
{
    FILE *fp;
    char cpath[512];

    build_path(cpath, sizeof(cpath), DATA_FILE);
    fp = fopen(cpath, "wb");
    if (!fp) { perror(cpath); return; }
    /* Create an empty file — no blank slots needed with sequential storage */
    fclose(fp);
    printf("'%s' created (empty).\n", cpath);
}
