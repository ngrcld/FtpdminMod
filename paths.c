//------------------------------------------------------------------------------------
// Module to handle mapping specified paths onto a relative path.
//------------------------------------------------------------------------------------
#include <stdio.h>
#include <stdlib.h>
#include <errno.h>
#include <ctype.h>
#include <string.h>
#include <winsock2.h>
#include <direct.h>

#include "paths.h"

static char RootDir[MAX_PATH]; // stored with backslashes.

//------------------------------------------------------------------------------------
// Specify root dir, specified with backslashes.
//------------------------------------------------------------------------------------
BOOL SetRootDir(char *dir) 
{
    if (chdir(dir)) return FALSE;
    getcwd(RootDir, sizeof(RootDir));
    printf("Using '%s' as root directory\n",RootDir);
    return TRUE;
}
//------------------------------------------------------------------------------------
// Specify directory change.  Specified with forward slashes.
//------------------------------------------------------------------------------------
BOOL MySetDir(char *dir)
{
    int a;
    char DirWas[MAX_PATH];
    char DirNow[MAX_PATH];

    if (dir[0] == 0) return TRUE;

    getcwd(DirWas, sizeof(DirWas));
    for (a=0;dir[a];a++){
        if (dir[a] == '/') dir[a] = '\\';
    }
    if (dir[0] == '\\'){
        // Set absolute directory.  Start from our root.
        chdir(RootDir);
        dir += 1;
    }

    if (dir[0] == 0) return TRUE;
    
    // Set relative.
    if (chdir(dir)){
        // Chdir failed.
        chdir(DirWas); // Go back to previous directory.
        return FALSE;
    }

    getcwd(DirNow, sizeof(DirNow));

    if (memcmp(DirNow, RootDir, strlen(RootDir))){
        // We are no longer unter 'root'.
        chdir(DirWas);
        return FALSE;
    }        
    return TRUE;
}

//------------------------------------------------------------------------------------
// get current directory.  Directory is returned with forward slashes.
//------------------------------------------------------------------------------------
char *MyGetDir(void)
{
    int a;
    int RootLen;
    static char DirNow[MAX_PATH];

    RootLen = strlen(RootDir);
    if (RootLen ==3) RootLen = 2;

    getcwd(DirNow, sizeof(DirNow));
    strcpy(DirNow, DirNow+RootLen);
    for (a=0;DirNow[a];a++){
        if (DirNow[a] == '\\') DirNow[a] = '/';
    }
    
    // If dir length is zero, we are at root, but that is indicated by '/'
    if (DirNow[0] == 0){
        strcpy(DirNow, "/");
    }

    return DirNow;
}

//------------------------------------------------------------------------------------
// Check for path bits that are all '.'  These are illegal filenames under
// dos, or would allow us to go outside of the 'root' specified.
//------------------------------------------------------------------------------------
char *TranslatePath(const char *FilePath)
{
    int a,b;
    BOOL NonDot;
    static char NewPath[MAX_PATH];

    NonDot = FALSE;

    if (FilePath[0] == '\\' || FilePath[0] == '/'){
        // Absolute file path.  Concatenate path with our notion of root
        strcpy(NewPath, RootDir);
        b = strlen(RootDir);
        if (b == 3) b = 2; // Root dir is something like 'c:\' - so it ends with '\'.  
    }else{
        // Relative path. 
        b = 0;
    }

    NonDot = FALSE;
    for (a=0;;a++){
        if (FilePath[a] == '/' || FilePath[a] == '\\'){
            NewPath[b] = '\\';
            if (!NonDot && a != 0) return NULL;
            NonDot = FALSE;
        }else{
            if (FilePath[a] != '.'){
                NonDot = TRUE;
            }
            NewPath[b] = FilePath[a];
        }
        if (FilePath[a] == 0) break;
        b++;
    }
    if (!NonDot) return NULL;

    return NewPath;
}
