#ifndef _GNU_SOURCE
#define _GNU_SOURCE 1
#endif

#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <time.h>
#include <unistd.h>
#include <zlib.h>

#include "khash.h"
#include "kseq.h"

KSEQ_INIT(gzFile, gzread)
KHASH_SET_INIT_STR(str)

void print_usage(char **argv) {
    fprintf(stderr, "Usage: %s [-dn] [-c chunk_size] [-o output.fastq] i1.fastq.gz r1.fastq.gz r2.fastq.gz\n", argv[0]);
}


int main(int argc, char **argv) {
    bool debugMode = false;
    bool dedup = true;
    int chunkSize = 0;
    char *filenameOutputOption = NULL;
    char *experimentIDOption = NULL;
    int option = 0;

    opterr = 0;

    while ((option = getopt(argc, argv, "c:de:no:")) != -1) {
        switch (option) {
            case 'c':
                chunkSize = atoi(optarg);
                break;
            case 'd':
                debugMode = true;
                break;
            case 'e':
                asprintf(&experimentIDOption, "%s", optarg);
                break;
            case 'n':
                dedup = false;
                break;
            case 'o':
                asprintf(&filenameOutputOption, "%s", optarg);
                break;
            default:
                print_usage(argv);
                exit(EXIT_FAILURE);
        }
    }

    char *experimentID = NULL;

    /* sample barcode */
    char* filenameI1 = NULL;

    /* cell barcode + UMI */
    char* filenameR1 = NULL;

    /* reads */
    char* filenameR2 = NULL;

    /* output file */
    char* filenameOutputFASTQ = NULL;

    if ((argc - optind) == 3) {
        filenameI1 = argv[optind];
        filenameR1 = argv[optind+1];
        filenameR2 = argv[optind+2];
    } else {
        print_usage(argv);
        exit(EXIT_FAILURE);
    }


    if ((chunkSize != 0) && (!filenameOutputOption)) {
        fprintf(stderr, "-c cannot be specified when using stdout");
        exit(EXIT_FAILURE);
    }

    if (debugMode) {
        fprintf(stderr, "I1: %s\n", filenameI1);
        fprintf(stderr, "R1: %s\n", filenameR1);
        fprintf(stderr, "R2: %s\n", filenameR2);

        if (filenameOutputOption) {
            fprintf(stderr, "Output: %s\n", filenameOutputOption);
        }

        fprintf(stderr, "Chunk size: %d\n", chunkSize);

        if (dedup) {
            fprintf(stderr, "Dedup mode: ON\n");
        } else {
            fprintf(stderr, "Dedup mode: OFF\n");
        }
    }

    // timers for debugging
    clock_t timeStart;
    clock_t timeChunk;
    clock_t timeTemp;

    timeStart = clock();
    timeChunk = clock();

    /* setup I1 */
    gzFile gzI1;
    kseq_t *seqI1;
    FILE *fpI1 = fopen(filenameI1, "rb");

    if (fpI1 == NULL) {
        fprintf(stderr, "Can't open: %s\n", filenameI1);
        exit(EXIT_FAILURE);
    }

    gzI1 = gzdopen(fileno(fpI1), "r");
    seqI1 = kseq_init(gzI1);

    /* setup R1 */
    gzFile gzR1;
    kseq_t *seqR1;
    FILE *fpR1 = fopen(filenameR1, "rb");

    if (fpR1 == NULL) {
        fprintf(stderr, "Can't open: %s\n", filenameR1);
        exit(EXIT_FAILURE);
    }

    gzR1 = gzdopen(fileno(fpR1), "r");
    seqR1 = kseq_init(gzR1);

    /* setup R2 */
    gzFile gzR2;
    kseq_t *seqR2;
    FILE *fpR2 = fopen(filenameR2, "rb");

    if (fpR2 == NULL) {
        fprintf(stderr, "Can't open: %s\n", filenameR2);
        exit(EXIT_FAILURE);
    }

    gzR2 = gzdopen(fileno(fpR2), "r");
    seqR2 = kseq_init(gzR2);

    FILE *fpOut = stdout;

    if (filenameOutputOption) {
        if (chunkSize > 0) {
            asprintf(&filenameOutputFASTQ, "%s_0.fastq", filenameOutputOption);
        } else {
            asprintf(&filenameOutputFASTQ, "%s", filenameOutputOption);
        }
        fpOut = fopen(filenameOutputFASTQ, "w");
    }

    if (experimentIDOption) {
        asprintf(&experimentID, "%s", experimentIDOption);
    } else {
        if (filenameOutputOption) {
            asprintf(&experimentID, "%s", filenameOutputOption);
        } else {
            experimentID = "exp01";
        }
    }

    int chunkSizeDebug = 1000000;

    if (chunkSize > 0) {
        chunkSizeDebug = chunkSize;
    }

    khint_t k;

    // allocate a hash table
    khash_t(str) *h = kh_init(str);

    /*
    CR = R1.sequence[:16]
    CY = R1.quality[:16]
    UR = R1.sequence[16:]
    UY = R1.quality[16:]
    */

    char cr[17];
    char cy[17];
    char ur[11];
    char uy[11];
    char *bc = NULL;   // I1.seq
    char *qt = NULL;   // I1.qual
    memset(cr, '\0', 17);
    memset(cy, '\0', 17);
    memset(ur, '\0', 11);
    memset(uy, '\0', 11);

    //name, comment, seq, qual;
    int nCounter = 0;
    int nFiles = 0;

    while (kseq_read(seqR1) >= 0) {
        kseq_read(seqR2);
        kseq_read(seqI1);

        if (dedup) {
            //int buf_size = seqR1->seq.l + seqR2->seq.l + seqI1->seq.l;
            int buf_size = seqR1->seq.l;// + seqI1->seq.l;
            char *buf = malloc(buf_size);
            //snprintf(buf, buf_size, "%s%s%s", seqR1->seq.s, seqR2->seq.s, seqI1->seq.s);
            snprintf(buf, buf_size, "%s", seqR1->seq.s);

            int absent;
            k = kh_put(str, h, buf, &absent);

            if (absent) {
                kh_key(h, k) = strdup(buf);
            } else {
                continue;
            }
        }

        memcpy(cr, &seqR1->seq.s[0], 16);
        memcpy(cy, &seqR1->qual.s[0], 16);
        memcpy(ur, &seqR1->seq.s[16], 10);
        memcpy(uy, &seqR1->qual.s[16], 10);

        bc = seqI1->seq.s;
        qt = seqI1->qual.s;

        //fprintf(fpOut, "@%s|||CR|||%s|||CY|||%s|||UR|||%s|||UY|||%s|||BC|||%s|||QT|||%s|||CID|||%s:%s:%s %s\n",
        //        seqR1->name.s, cr, cy, ur, uy, bc, qt, experimentID, bc, cr, seqR2->comment.s);
        fprintf(fpOut, "@%s|||CR|||%s|||CY|||%s|||UR|||%s|||UY|||%s|||BC|||%s|||QT|||%s|||CID|||%s-%s %s\n",
                seqR1->name.s, cr, cy, ur, uy, bc, qt, cr, experimentID, seqR2->comment.s);
        fprintf(fpOut, "%s\n+\n%s\n", seqR2->seq.s, seqR2->qual.s);

        nCounter = nCounter + 1;

        if ((debugMode) && ((nCounter % chunkSizeDebug) == 0)) {
            timeTemp = clock() - timeStart;
            double time_total = ((double)timeTemp)/CLOCKS_PER_SEC; // in seconds

            timeTemp = clock() - timeChunk;
            double time_chunk = ((double)timeTemp)/CLOCKS_PER_SEC; // in seconds

            fprintf(stderr, "Reads: %d, Chunk Time: %f, Total Time: %f\n", nCounter, time_chunk, time_total);

            timeChunk = clock();
        }

        if ((chunkSize > 0) && (nCounter % chunkSize) == 0) {
            fclose(fpOut);
            nFiles = nFiles + 1;

            if (debugMode) {
                fprintf(stderr, "Generated: %s\n", filenameOutputFASTQ);
            }

            asprintf(&filenameOutputFASTQ, "%s_%d.fastq", filenameOutputOption, nFiles);

            if (debugMode) {
                fprintf(stderr, "Generating: %s\n", filenameOutputFASTQ);
            }

            fpOut = fopen(filenameOutputFASTQ, "w");
        }
    }

    kseq_destroy(seqI1);
    fclose(fpI1);
    gzclose(gzI1);

    kseq_destroy(seqR1);
    fclose(fpR1);
    gzclose(gzR1);

    kseq_destroy(seqR2);
    fclose(fpR2);
    gzclose(gzR2);

    fclose(fpOut);

    if (debugMode) {
        fprintf(stderr, "Generated: %s\n", filenameOutputFASTQ);

        timeTemp = clock() - timeStart;
        double time_total = ((double)timeTemp)/CLOCKS_PER_SEC; // in seconds

        fprintf(stderr, "Done. Reads: %d, Total Time: %f\n", nCounter, time_total);
    }

    // deallocate the hash table
    kh_destroy(str, h);

    return 0;
}