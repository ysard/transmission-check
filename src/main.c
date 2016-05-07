
//gcc -Wall -Wextra -L./lib -I./include src/main.c -o main -ltransmission
//gcc -Wall -Wextra -L./lib -I./include src/main.c -o main -ltransmission -lz -levent -lpthread -lssl -lcrypto -lcurl
// On pourrait aussi ajouter libnatpmp, miniupnp au lieu d'installer libnatpmp-dev  sur le système
//gcc -Wall -Wextra -L./lib -L./include/dht -L./include/libutp -I./include src/main.c -o main -ltransmission -lz -levent -lpthread -lssl -lcrypto -lcurl -lnatpmp -lminiupnpc -lutp -ldht

#include <locale.h>
#include <signal.h>
#include <string.h>
#include <stdio.h>
#include <stdlib.h> /* exit () */
#include <time.h>

// uint64_t on printf()
// http://en.cppreference.com/w/cpp/types/integer
#include <inttypes.h>

#include <sys/types.h>
#include <sys/stat.h> // stat()
#include <ftw.h> // ftw()
#include <regex.h> // regexec(), regerror(), regfree(), regcomp()
#include <libgen.h> // basename()

#include <errno.h>
#include <unistd.h>

#include <libtransmission/transmission.h>
#include <libtransmission/variant.h>
#include <libtransmission/tr-getopt.h>
//#include <libtransmission/rpcimpl.h>
//#include <libtransmission/utils.h>
//#include <libtransmission/version.h>

#define MY_NAME "transmission-check"
#define LONG_VERSION_STRING "0.1"

static uint64_t total_size = 0;
static int nb_repaired_inconsistencies = 0;

// Parameters
static bool make_changes = false;
static bool showVersion = false;
static const char * resume_file = NULL;
static const char * replace[2] = { NULL, NULL };

static tr_option options[] =
{
    { 'm', "make-changes", "Make changes on resume file", "m", 0, NULL },
    { 'r', "replace", "Search and replace a substring in the file paths", "r", 1, "<old> <new>" },
    { 'V', "version", "Show version number and exit", "V", 0, NULL },
    { 0, NULL, NULL, NULL, 0, NULL }
};


static const char * getUsage (void)
{
    return "Usage: " MY_NAME " [options] resume-file";
}


static int parseCommandLine (int argc, const char ** argv)
{
    int c;
    const char * optarg;

    while ((c = tr_getopt (getUsage (), argc, argv, options, &optarg)))
    {
        switch (c)
        {
            case 'm':
                make_changes = true;
                break;

            case 'r':
                replace[0] = optarg;
                c = tr_getopt (getUsage (), argc, argv, options, &optarg);
                if (c != TR_OPT_UNK)
                    return 1;
                replace[1] = optarg;
                break;

            case 'V':
                showVersion = true;
                break;

            case TR_OPT_UNK:
                if (resume_file != NULL)
                    return 1;
                resume_file = optarg;
                break;

            default:
                return 1;
        }
    }

    return 0;
}


int is_file_or_dir_exists(const char *path)
{
    /* Detect if the given file/directory exists.
     * Return 0 if does not exist. 1,2 or 3 if exists and is a directory, symlink, regular file.
     */

    struct stat info;
    int err = 0;

    // On success, zero is returned. On error, -1 is returned, and errno is set appropriately.
    err = stat(path, &info);

    if(err == -1) {
        if(errno == ENOENT) {
            /* does not exist */
            return 0;
        } else {
            perror("stat");
            exit(EXIT_FAILURE);
        }
    }

    // directory : info.st_mode & S_IFDIR
    switch (info.st_mode & S_IFMT) {
        case S_IFDIR:  printf("Directory found... ");        return 1;
        case S_IFLNK:  printf("Symlink found... ");          return 2;
        case S_IFREG:  printf("Regular file found... ");     return 3;
        default:       printf("Unknown type?\n");            return 0;
    }

    return 0;
}


int sum_sizes(const char *fpath __attribute__((unused)), const struct stat *sb, int typeflag __attribute__((unused)))
{
    /* Callback for ftw() function.
     * Calculate the size of the given file and increment static 'total_size' variable.
     */

    total_size += sb->st_size;
    return 0;
}


void check_uploaded_files(char ** full_path)
{
    /* Test the existence of the given file/directory.
     * Calculate the size.
     */

    int err = 0;

    if (is_file_or_dir_exists(*full_path) > 0) {

        err = ftw(*full_path, &sum_sizes, 1);

        if (err == -1) {
            perror("ftw");
            exit(EXIT_FAILURE);
        }

    } else {
        fprintf(stderr, "ERROR: Uploaded file/directory '%s' not found !\n", *full_path);
        exit(EXIT_FAILURE);
    }

    printf("Total bytes: %" PRIu64 "\n", total_size);
}


void get_uploaded_files_path(tr_variant * top, char ** full_path)
{
    /* Compute the full path of file/directory downloaded by the torrent.
     */

    char * tmp_ptr = NULL;
    size_t len;
    const char * str;

    // Concatenate paths dynamically.
    if ((tr_variantDictFindStr (top, TR_KEY_destination, &str, &len))
        && (str && *str))
    {
        //printf("TR_KEY_destination %s, %zu\n", str, len);

        *full_path = malloc(len * sizeof(**full_path) + 1);

        strcpy(*full_path, str);
        //printf("dir path: %s, %zu\n", *full_path, strlen(*full_path));

        if (tr_variantDictFindStr (top, TR_KEY_name, &str, &len))
        {
            //printf("TR_KEY_name %s, %zu\n", str, len);

            // Temp pointer
            tmp_ptr = realloc(*full_path, strlen(*full_path) + len + 2);

            if(tmp_ptr == NULL){
                free(*full_path);    //Deallocation
                exit(EXIT_FAILURE);
            } else{
                *full_path = tmp_ptr;
            }

            strcat(*full_path, "/");
            strcat(*full_path, str);

            //printf("full path: %s\n", *full_path);
            return;

        } else {
            fprintf(stderr, "ERROR: Resume file: TR_KEY_name could not be read !\n");
        }

    } else {
        fprintf(stderr, "ERROR: Resume file: TR_KEY_destination could not be read !\n");
    }

    // On error, deallocate the memory
    free(*full_path);
    exit(EXIT_FAILURE);
}


void check_dates(tr_variant * top, char ** full_path, bool make_changes)
{
    /* Try to resolve date problems (incorrect/corrupted dates)
     * On error => update the field with last file modification date.
     */

    struct stat sb;
    int err = 0;
    int64_t  i;
    struct tm instant;


    err = stat(*full_path, &sb);

    if(err == -1) {
        perror("stat");
        exit(EXIT_FAILURE);
    }

    /*
    printf("Last status change:       %s", ctime(&sb.st_ctime));
    printf("Last file access:         %s", ctime(&sb.st_atime));
    printf("Last file modification:   %s", ctime(&sb.st_mtime));
    */

    if (tr_variantDictFindInt (top, TR_KEY_added_date, &i))
    {
        instant = *localtime(&i);
        //printf("TR_KEY_added_date %s %" PRIu64 "\n", ctime(&i), i);

        if (instant.tm_year + 1900 == 1970) {
            // temps exec / added date => file ?? modif...
            if (make_changes) {
                tr_variantDictAddInt(top, TR_KEY_added_date, sb.st_mtime);
                printf("REPAIR: Erroneous added date: Updated to modification date: %s", ctime(&i));

                nb_repaired_inconsistencies++;
            } else {
                printf("Erroneous added date detected !\n");
            }
        }
    }

    if (tr_variantDictFindInt (top, TR_KEY_done_date, &i))
    {
        instant = *localtime(&i);
        //printf("TR_KEY_done_date %s %" PRIu64 "\n", ctime(&i), i);

        if (instant.tm_year + 1900 == 1970) {
            // temps exec / done date => file modif
            if (make_changes) {
                tr_variantDictAddInt(top, TR_KEY_added_date, sb.st_mtime);
                printf("REPAIR: Erroneous done date: Updated to modification date: %s", ctime(&i));

                nb_repaired_inconsistencies++;
            } else {
                printf("Erroneous done date detected !\n");
            }
        }
    }

    /*
    if (tr_variantDictFindInt (top, TR_KEY_activity_date, &i))
    {
        instant=*localtime(&i);
        printf("TR_KEY_activity_date %s \b%" PRIu64 "\n", ctime(&i), i);
    }*/
}

/*
void check_sizes(tr_variant * top, char ** full_path, bool make_changes)
{




}
*/

void reset_peers(tr_variant * top)
{
    size_t len;
    const uint8_t * str_8;

    if (tr_variantDictFindRaw (top, TR_KEY_peers2, &str_8, &len))
    {
        // reinit peers
        tr_variantDictAddRaw (top, TR_KEY_peers2, NULL, 0);
    }

    if (tr_variantDictFindRaw (top, TR_KEY_peers2_6, &str_8, &len))
    {
        // reinit peers
        tr_variantDictAddRaw (top, TR_KEY_peers2_6, NULL, 0);
    }

    printf("REPAIR: Peers cleared.");
}


void check_correct_files_pointed(tr_variant * top, const char resume_filename[], bool make_changes)
{
    size_t len;
    int err = 0;
    regex_t preg;
    const char * actual_file;
    const char * regex_suffix = "\\.([[:lower:][:digit:]]{16})\\.resume$";


    // Get file downloaded
    tr_variantDictFindStr(top, TR_KEY_name, &actual_file, &len);
    //printf("%s VS %s\n", actual_file, resume_filename);

    // No pb in file names
    if(strstr(resume_filename, actual_file) != NULL) {
        //printf("File/directory name matches !\n");
        return;
    }


    fprintf(stderr, "Resume file does not point to the correct file/directory !\n");


    if (make_changes == 0)
        return;

    printf("REPAIR: Trying to resolve inconsistencies...\n");


    err = regcomp (&preg, regex_suffix, REG_EXTENDED);

    if (err == 0) {

        int match;
        size_t nmatch = 0;
        regmatch_t *pmatch = NULL;

        nmatch = preg.re_nsub;
        pmatch = malloc (sizeof (*pmatch) * nmatch);

        // Now, we know that there are n matches.
        if (pmatch) {

            match = regexec (&preg, resume_filename, nmatch, pmatch, 0);
            regfree (&preg);

            if (match == 0) {

                char * inferred_file = NULL;
                //char * suffix = NULL;
                int start = pmatch[0].rm_so;
                //int end = pmatch[0].rm_eo;
                //size_t size = end - start;

                /*
                suffix = malloc (sizeof (*suffix) * (size + 1));

                if (suffix != NULL) {
                    strncpy(suffix, &resume_filename[start], size);
                    suffix[size] = '\0';
                    printf("suffix %s\n", suffix);
                    free(suffix);
                }
                */

                //printf("suffix found at position %d\n", start);
                // 0 to x included, where x is the position of the first character of the suffix
                inferred_file = malloc (sizeof (*inferred_file) * (start));

                if (inferred_file != NULL) {
                    strncpy(inferred_file, &resume_filename[0], start);
                    inferred_file[start] = '\0';
                    printf("REPAIR: Inferred file: %s\n", inferred_file);

                    // Update the resume file
                    tr_variantDictAddStr(top, TR_KEY_name, inferred_file);
                    nb_repaired_inconsistencies++;

                    // Deallocate memory
                    free(inferred_file);
                }

            } else if (match == REG_NOMATCH) {
                fprintf (stderr, "ERROR: Resume file has an incorrect name !\n");
                exit(EXIT_FAILURE);

            } else {
                char *text;
                size_t size;

                size = regerror(err, &preg, NULL, 0);
                text = malloc(sizeof (*text) * size);

                if (text) {

                    regerror (err, &preg, text, size);
                    fprintf (stderr, "ERROR: Regex: %s\n", text);
                    free(text);

                } else {
                    fprintf (stderr, "ERROR: Insufficient memory\n\n");
                    exit(EXIT_FAILURE);
                }
            }
        } else {
            fprintf (stderr, "ERROR: Insufficient memory\n\n");
            exit(EXIT_FAILURE);
        }
    }
}


void read_resume_file(tr_variant * top)
{
    size_t len;
    int64_t  i;
    const char * str;
    tr_variant * dict;
    bool boolVal;


    printf("\n==============================\n");
    printf("   Resume file informations   \n");
    printf("==============================\n\n");


    if ((tr_variantDictFindStr (top, TR_KEY_destination, &str, &len))
        && (str && *str))
    {
        printf("TR_KEY_destination %s\n", str);
        //tr_strndup (str, len);
    }

    if ((tr_variantDictFindStr (top, TR_KEY_incomplete_dir, &str, &len))
        && (str && *str))
    {
        printf("TR_KEY_incomplete_dir %s\n", str);
    }

    if (tr_variantDictFindStr (top, TR_KEY_name, &str, NULL))
    {
        printf("TR_KEY_name %s\n", str);
    }

    if (tr_variantDictFindInt (top, TR_KEY_downloaded, &i))
    {
        printf("TR_KEY_downloaded %" PRIu64 "\n", i);
        //tr_variantDictAddInt (top, TR_KEY_downloaded, i + 1024);
    }

    if (tr_variantDictFindInt (top, TR_KEY_uploaded, &i))
    {
        printf("TR_KEY_uploaded %" PRIu64 "\n", i);
    }

    if (tr_variantDictFindInt (top, TR_KEY_max_peers, &i))
    {
        printf("TR_KEY_max_peers %" PRIu64 "\n", i);
    }

    if (tr_variantDictFindBool (top, TR_KEY_paused, &boolVal))
    {
        printf("TR_KEY_paused %d\n", boolVal);
    }

    if (tr_variantDictFindInt (top, TR_KEY_added_date, &i))
    {
        struct tm instant;
        instant=*localtime((time_t*)&i);
        printf("TR_KEY_added_date %" PRIu64 "\n", i);
        printf("%d/%d/%d ; %d:%d:%d\n", instant.tm_mday+1, instant.tm_mon+1, instant.tm_year+1900, instant.tm_hour, instant.tm_min, instant.tm_sec);
    }

    if (tr_variantDictFindInt (top, TR_KEY_done_date, &i))
    {
        printf("TR_KEY_done_date %" PRIu64 "\n", i);
        struct tm instant;
        instant=*localtime((time_t*)&i);
        printf("%d/%d/%d ; %d:%d:%d\n", instant.tm_mday+1, instant.tm_mon+1, instant.tm_year+1900, instant.tm_hour, instant.tm_min, instant.tm_sec);
    }

    if (tr_variantDictFindInt (top, TR_KEY_activity_date, &i))
    {
        printf("TR_KEY_activity_date %" PRIu64 "\n", i);
        struct tm instant;
        instant=*localtime((time_t*)&i);
        printf("%d/%d/%d ; %d:%d:%d\n", instant.tm_mday+1, instant.tm_mon+1, instant.tm_year+1900, instant.tm_hour, instant.tm_min, instant.tm_sec);
    }

    if (tr_variantDictFindInt (top, TR_KEY_seeding_time_seconds, &i))
    {
        printf("TR_KEY_seeding_time_seconds %" PRIu64 "\n", i);
    }

    if (tr_variantDictFindInt (top, TR_KEY_downloading_time_seconds, &i))
    {
        printf("TR_KEY_downloading_time_seconds %" PRIu64 "\n", i);
    }

    if (tr_variantDictFindInt (top, TR_KEY_bandwidth_priority, &i)
        /*&& tr_isPriority (i)*/)
    {
        printf("TR_KEY_bandwidth_priority %" PRIu64 "\n", i);
    }



    if (tr_variantDictFindDict (top, TR_KEY_speed_limit_up, &dict))
    {
        printf("Speed limit up:\n");

        if (tr_variantDictFindInt (dict, TR_KEY_speed_Bps, &i))
        {
            printf("\tTR_KEY_speed_Bps %" PRIu64 "\n", i);
        }
        else if (tr_variantDictFindInt (dict, TR_KEY_speed, &i))
            printf("\tTR_KEY_speed %" PRIu64 "\n", i*1024);

        if (tr_variantDictFindBool (dict, TR_KEY_use_speed_limit, &boolVal))
            printf("\tTR_KEY_use_speed_limit %d\n", boolVal);

        if (tr_variantDictFindBool (dict, TR_KEY_use_global_speed_limit, &boolVal))
            printf("\tTR_KEY_use_global_speed_limit %d\n", boolVal);
    }

    if (tr_variantDictFindDict (top, TR_KEY_speed_limit_down, &dict))
    {
        printf("Speed limit down:\n");

        if (tr_variantDictFindInt (dict, TR_KEY_speed_Bps, &i))
        {
            printf("\tTR_KEY_speed_Bps %" PRIu64 "\n", i);
        }
        else if (tr_variantDictFindInt (dict, TR_KEY_speed, &i))
            printf("\tTR_KEY_speed %" PRIu64 "\n", i*1024);

        if (tr_variantDictFindBool (dict, TR_KEY_use_speed_limit, &boolVal))
            printf("\tTR_KEY_use_speed_limit %d\n", boolVal);

        if (tr_variantDictFindBool (dict, TR_KEY_use_global_speed_limit, &boolVal))
            printf("\tTR_KEY_use_global_speed_limit %d\n", boolVal);
    }


    /*
     i *f (fieldsToLoad & TR_FR_PEERS)
     fieldsLoaded |= loadPeers (top, tor);
     */
    const uint8_t * str_8;

    if (tr_variantDictFindRaw (top, TR_KEY_peers2, &str_8, &len))
    {
        printf("TR_KEY_peers2 %zu bytes\n", len);
    }

    if (tr_variantDictFindRaw (top, TR_KEY_peers2_6, &str_8, &len))
    {
        printf("TR_KEY_peers2_6 %zu bytes\n", len);
    }
    /*
     i *f (fieldsToLoad & TR_FR_FILE_PRIORITIES)
     fieldsLoaded |= loadFilePriorities (top, tor);

     if (fieldsToLoad & TR_FR_PROGRESS)
         fieldsLoaded |= loadProgress (top, tor);

     if (fieldsToLoad & TR_FR_DND)
         fieldsLoaded |= loadDND (top, tor);

     if (fieldsToLoad & TR_FR_RATIOLIMIT)
         fieldsLoaded |= loadRatioLimits (top, tor);

     if (fieldsToLoad & TR_FR_IDLELIMIT)
         fieldsLoaded |= loadIdleLimits (top, tor);

     if (fieldsToLoad & TR_FR_FILENAMES)
         fieldsLoaded |= loadFilenames (top, tor);

     if (fieldsToLoad & TR_FR_NAME)
         fieldsLoaded |= loadName (top, tor);

     */

    tr_variant * list;

    if (tr_variantDictFindList (top, TR_KEY_files, &list))
    {
        size_t i;
        const size_t n = tr_variantListSize (list);
        printf("TR_KEY_files ici\n");

        for (i=0; i<n; ++i)
        {
            //const char * str;
            size_t str_len;
            if (tr_variantGetStr (tr_variantListChild (list, i), &str, &str_len) && str && str_len)
            {
                printf("TR_KEY_files %s\n", str);
            }
        }
    }
}


void repair_resume_file(tr_variant * top, char resume_filename[], bool make_changes)
{


    printf("\n\n==============================\n");
    printf("        Repair attempts       \n");
    printf("==============================\n\n");



    char * full_path;
    get_uploaded_files_path(top, &full_path);
    printf("Full path: %s\n", full_path);

    check_correct_files_pointed(top, resume_filename, make_changes);

    if (nb_repaired_inconsistencies > 0) {
        get_uploaded_files_path(top, &full_path);
        printf("REPAIR: New full path: %s\n", full_path);
    }

    check_uploaded_files(&full_path);
    check_dates(top, &full_path, make_changes);

    // If there are inconsistencies, the file is corrupted => cleaning step
    if ((nb_repaired_inconsistencies > 0) && make_changes)
        reset_peers(top);
    //check_sizes(&top, &full_path, make_changes);


    printf("Repaired inconsistencies: %d\n", nb_repaired_inconsistencies);

    // Free memory
    free(full_path);
}


int main (int argc, char ** argv)
{

    if (parseCommandLine (argc, (const char**)argv))
        return EXIT_FAILURE;

    printf("%d, %d, %s, %s, %s\n", showVersion, make_changes, resume_file, replace[0], replace[1]);

    if (showVersion)
    {
        fprintf(stderr, MY_NAME" "LONG_VERSION_STRING"\n");
        return EXIT_SUCCESS;
    }

    if (resume_file == NULL)
    {
        fprintf (stderr, "ERROR: No resume file specified.\n");
        tr_getopt_usage (MY_NAME, getUsage (), options);
        fprintf (stderr, "\n");
        return EXIT_FAILURE;
    }

    if (make_changes && (replace[0] != NULL))
    {
        fprintf (stderr, "ERROR: Must specify -m or -r\n");
        tr_getopt_usage (MY_NAME, getUsage (), options);
        fprintf (stderr, "\n");
        return EXIT_FAILURE;
    }


    char * resume_filename = NULL;
    tr_variant top;
    int err;

    // cast to non-const
    resume_filename = basename((char*)resume_file);
    //printf("resume file: %s\n", resume_filename);

    // Load the resume file in memory
    if (tr_variantFromFile (&top, TR_VARIANT_FMT_BENC, resume_file))
    {
        fprintf(stderr, "ERROR: Resume file could not be opened !\n");
        exit(EXIT_FAILURE);
    }

    // Load infos from resume file
    read_resume_file(&top);

    // Repair attempts
    repair_resume_file(&top, resume_filename, make_changes);


    // Write the resume file if inconsistencies are repaired, and if changes are allowed
    if (nb_repaired_inconsistencies > 0 && make_changes && (err = tr_variantToFile(&top, TR_VARIANT_FMT_BENC, resume_file)))
    {
        fprintf(stderr, "ERROR: While saving the new .resume file\n");
    }

    // Free memory
    tr_variantFree (&top);
    exit(EXIT_SUCCESS);
}