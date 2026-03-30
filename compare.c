#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <dirent.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <math.h>

char *my_strdup(const char *s) {
    int len = strlen(s) + 1;
    char *copy = malloc(len);

    if (copy == NULL) {
        fprintf(stderr, "malloc failed\n");
        exit(EXIT_FAILURE);
    }

    strcpy(copy, s);
    return copy;
}
#define SUFFIX ".txt"
#define BUFFER_SIZE 4096

typedef struct WordNode {
    char *word;
    int count;
    double freq;
    struct WordNode *next;
} WordNode;

typedef struct FileInfo {
    char *path;
    WordNode *words;
    int totalWords;
} FileInfo;

typedef struct Comparison {
    char *file1;
    char *file2;
    int combinedWords;
    double jsd;
} Comparison;

static int hadError = 0;

/* ---------- file list storage ---------- */

FileInfo *files = NULL;
int fileCount = 0;
int fileCap = 0;

/* ---------- basic helpers ---------- */

int ends_with(const char *name, const char *suffix) {
    int nameLen = strlen(name);
    int suffixLen = strlen(suffix);

    if (nameLen < suffixLen) {
        return 0;
    }

    return strcmp(name + nameLen - suffixLen, suffix) == 0;
}

const char *get_base_name(const char *path) {
    const char *slash = strrchr(path, '/');
    if (slash == NULL) {
        return path;
    }
    return slash + 1;
}

int is_hidden(const char *path) {
    const char *name = get_base_name(path);
    return name[0] == '.';
}

char *make_path(const char *dir, const char *name) {
    int len1 = strlen(dir);
    int len2 = strlen(name);
    int needSlash = 1;

    if (len1 > 0 && dir[len1 - 1] == '/') {
        needSlash = 0;
    }

    char *full = malloc(len1 + len2 + needSlash + 1);
    if (full == NULL) {
        fprintf(stderr, "malloc failed\n");
        exit(EXIT_FAILURE);
    }

    strcpy(full, dir);
    if (needSlash) {
        strcat(full, "/");
    }
    strcat(full, name);

    return full;
}

/* ---------- word linked list ---------- */

WordNode *create_word_node(const char *word) {
    WordNode *node = malloc(sizeof(WordNode));
    if (node == NULL) {
        fprintf(stderr, "malloc failed\n");
        exit(EXIT_FAILURE);
    }

    node->word = my_strdup(word);
    if (node->word == NULL) {
        fprintf(stderr, "strdup failed\n");
        exit(EXIT_FAILURE);
    }

    node->count = 1;
    node->freq = 0.0;
    node->next = NULL;

    return node;
}

void add_word_sorted(WordNode **head, const char *word) {
    WordNode *prev = NULL;
    WordNode *curr = *head;

    while (curr != NULL && strcmp(curr->word, word) < 0) {
        prev = curr;
        curr = curr->next;
    }

    if (curr != NULL && strcmp(curr->word, word) == 0) {
        curr->count++;
        return;
    }

    WordNode *newNode = create_word_node(word);
    newNode->next = curr;

    if (prev == NULL) {
        *head = newNode;
    } else {
        prev->next = newNode;
    }
}

void free_word_list(WordNode *head) {
    WordNode *curr = head;
    while (curr != NULL) {
        WordNode *next = curr->next;
        free(curr->word);
        free(curr);
        curr = next;
    }
}

void calculate_frequencies(WordNode *head, int totalWords) {
    WordNode *curr = head;

    if (totalWords == 0) {
        return;
    }

    while (curr != NULL) {
        curr->freq = (double) curr->count / (double) totalWords;
        curr = curr->next;
    }
}

/* ---------- file storage ---------- */

int already_added(const char *path) {
    for (int i = 0; i < fileCount; i++) {
        if (strcmp(files[i].path, path) == 0) {
            return 1;
        }
    }
    return 0;
}

void add_file(const char *path) {
    if (already_added(path)) {
        return;
    }

    if (fileCount == fileCap) {
        int newCap;
        if (fileCap == 0) {
            newCap = 8;
        } else {
            newCap = fileCap * 2;
        }

        FileInfo *temp = realloc(files, newCap * sizeof(FileInfo));
        if (temp == NULL) {
            fprintf(stderr, "realloc failed\n");
            exit(EXIT_FAILURE);
        }

        files = temp;
        fileCap = newCap;
    }

    files[fileCount].path = my_strdup(path);
    if (files[fileCount].path == NULL) {
        fprintf(stderr, "strdup failed\n");
        exit(EXIT_FAILURE);
    }

    files[fileCount].words = NULL;
    files[fileCount].totalWords = 0;
    fileCount++;
}

void free_files(void) {
    for (int i = 0; i < fileCount; i++) {
        free(files[i].path);
        free_word_list(files[i].words);
    }
    free(files);
}

/* ---------- collecting files ---------- */

void collect_path(const char *path);

void collect_directory(const char *dirPath) {
    DIR *dir = opendir(dirPath);
    if (dir == NULL) {
        perror(dirPath);
        hadError = 1;
        return;
    }

    struct dirent *entry;

    while ((entry = readdir(dir)) != NULL) {
        if (entry->d_name[0] == '.') {
            continue;
        }

        char *childPath = make_path(dirPath, entry->d_name);

        struct stat st;
        if (stat(childPath, &st) < 0) {
            perror(childPath);
            hadError = 1;
            free(childPath);
            continue;
        }

        if (S_ISDIR(st.st_mode)) {
            collect_directory(childPath);
        } else if (S_ISREG(st.st_mode)) {
            if (ends_with(entry->d_name, SUFFIX)) {
                add_file(childPath);
            }
        }

        free(childPath);
    }

    closedir(dir);
}

void collect_path(const char *path) {
    if (is_hidden(path)) {
        return;
    }

    struct stat st;
    if (stat(path, &st) < 0) {
        perror(path);
        hadError = 1;
        return;
    }

    if (S_ISDIR(st.st_mode)) {
        collect_directory(path);
    } else if (S_ISREG(st.st_mode)) {
        /* explicit file args are always included */
        add_file(path);
    }
}

/* ---------- reading and tokenizing ---------- */

int is_word_char(unsigned char c) {
    return isalnum(c) || c == '-';
}

void read_file_words(FileInfo *file) {
    int fd = open(file->path, O_RDONLY);
    if (fd < 0) {
        perror(file->path);
        hadError = 1;
        return;
    }

    char buffer[BUFFER_SIZE];
    ssize_t bytesRead;

    char *word = NULL;
    int wordLen = 0;
    int wordCap = 0;

    while ((bytesRead = read(fd, buffer, BUFFER_SIZE)) > 0) {
        for (ssize_t i = 0; i < bytesRead; i++) {
            unsigned char c = (unsigned char) buffer[i];

            if (isspace(c)) {
                if (wordLen > 0) {
                    word[wordLen] = '\0';
                    add_word_sorted(&(file->words), word);
                    file->totalWords++;
                    wordLen = 0;
                }
            } else if (is_word_char(c)) {
                if (wordLen + 1 >= wordCap) {
                    int newCap;
                    if (wordCap == 0) {
                        newCap = 16;
                    } else {
                        newCap = wordCap * 2;
                    }

                    char *temp = realloc(word, newCap);
                    if (temp == NULL) {
                        fprintf(stderr, "realloc failed\n");
                        free(word);
                        close(fd);
                        exit(EXIT_FAILURE);
                    }

                    word = temp;
                    wordCap = newCap;
                }

                word[wordLen] = tolower(c);
                wordLen++;
            } else {
                /* punctuation is ignored */
            }
        }
    }

    if (bytesRead < 0) {
        perror(file->path);
        hadError = 1;
        free(word);
        close(fd);
        return;
    }

    if (wordLen > 0) {
        word[wordLen] = '\0';
        add_word_sorted(&(file->words), word);
        file->totalWords++;
    }

    calculate_frequencies(file->words, file->totalWords);

    free(word);
    close(fd);
}

/* ---------- jsd ---------- */

double compute_jsd(FileInfo *f1, FileInfo *f2) {
    WordNode *p1 = f1->words;
    WordNode *p2 = f2->words;

    double kld1 = 0.0;
    double kld2 = 0.0;

    while (p1 != NULL || p2 != NULL) {
        if (p1 != NULL && p2 != NULL) {
            int cmp = strcmp(p1->word, p2->word);

            if (cmp == 0) {
                double freq1 = p1->freq;
                double freq2 = p2->freq;
                double mean = (freq1 + freq2) / 2.0;

                if (freq1 > 0.0) {
                    kld1 += freq1 * log2(freq1 / mean);
                }
                if (freq2 > 0.0) {
                    kld2 += freq2 * log2(freq2 / mean);
                }

                p1 = p1->next;
                p2 = p2->next;
            } else if (cmp < 0) {
                double freq1 = p1->freq;
                double mean = freq1 / 2.0;

                if (freq1 > 0.0) {
                    kld1 += freq1 * log2(freq1 / mean);
                }

                p1 = p1->next;
            } else {
                double freq2 = p2->freq;
                double mean = freq2 / 2.0;

                if (freq2 > 0.0) {
                    kld2 += freq2 * log2(freq2 / mean);
                }

                p2 = p2->next;
            }
        } else if (p1 != NULL) {
            double freq1 = p1->freq;
            double mean = freq1 / 2.0;

            if (freq1 > 0.0) {
                kld1 += freq1 * log2(freq1 / mean);
            }

            p1 = p1->next;
        } else {
            double freq2 = p2->freq;
            double mean = freq2 / 2.0;

            if (freq2 > 0.0) {
                kld2 += freq2 * log2(freq2 / mean);
            }

            p2 = p2->next;
        }
    }

    return sqrt((kld1 + kld2) / 2.0);
}

/* ---------- sorting ---------- */

int compare_results(const void *a, const void *b) {
    const Comparison *x = (const Comparison *) a;
    const Comparison *y = (const Comparison *) b;

    if (x->combinedWords < y->combinedWords) {
        return 1;
    }
    if (x->combinedWords > y->combinedWords) {
        return -1;
    }

    if (x->jsd < y->jsd) {
        return -1;
    }
    if (x->jsd > y->jsd) {
        return 1;
    }

    return 0;
}

/* ---------- main ---------- */

int main(int argc, char *argv[]) {
    if (argc < 2) {
        fprintf(stderr, "Usage: %s file_or_directory ...\n", argv[0]);
        return EXIT_FAILURE;
    }

    for (int i = 1; i < argc; i++) {
        collect_path(argv[i]);
    }

    if (fileCount < 2) {
        fprintf(stderr, "Error: fewer than two files found\n");
        free_files();
        return EXIT_FAILURE;
    }

    for (int i = 0; i < fileCount; i++) {
        read_file_words(&files[i]);
    }

    int pairCount = fileCount * (fileCount - 1) / 2;

    Comparison *results = malloc(pairCount * sizeof(Comparison));
    if (results == NULL) {
        fprintf(stderr, "malloc failed\n");
        free_files();
        exit(EXIT_FAILURE);
    }

    int index = 0;
    for (int i = 0; i < fileCount; i++) {
        for (int j = i + 1; j < fileCount; j++) {
            results[index].file1 = files[i].path;
            results[index].file2 = files[j].path;
            results[index].combinedWords = files[i].totalWords + files[j].totalWords;
            results[index].jsd = compute_jsd(&files[i], &files[j]);
            index++;
        }
    }

    qsort(results, pairCount, sizeof(Comparison), compare_results);

    for (int i = 0; i < pairCount; i++) {
        printf("%.5f %s %s\n",
               results[i].jsd,
               results[i].file1,
               results[i].file2);
    }

    free(results);
    free_files();

    if (hadError) {
        return EXIT_FAILURE;
    }

    return EXIT_SUCCESS;
}