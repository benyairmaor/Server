#include <stdio.h>
#include <stdlib.h>
#include <netinet/in.h>
#include <string.h>
#include <sys/socket.h>
#include <unistd.h>
 #include <dirent.h>
#include <sys/types.h>
#include <time.h>
#include <sys/stat.h>
#include <fcntl.h>
#include "threadpool.h"

// server that handle request from client from browser or telnet. Gets from user port, num of max tread and num of max request. The server send the next following responses: 
// 	1. 400 - If the it is bad request.
// 	2. 501 - If this is not GET method.
// 	3. 404 - If file not found.
// 	4. 302 - If the path is Dir but has no Slash at the end.
// 	5. 403 - If there is no premissin.
// 	6. 500 - If there was an Error while the server is running.
// 	7. Dir content - If the path is Dir and end with slash.
// 	8. return the file if the file is REG and has no slash.
// In any case of Dir content or file returned the following response will be 200 OK.

#define RFC1123FMT "%a, %d %b %Y %H:%M:%S GMT"



int appendCurrentTime(char **request){ //add to the response the current time. gets the request.
    int length = strlen(*request) + 1;
    time_t now;
    char timebuf[128];
    now = time(NULL);
    strftime(timebuf, sizeof(timebuf), RFC1123FMT, gmtime(&now));
    length += strlen(timebuf);
    *request = (char *)realloc(*request, length);
    strcat(*request, timebuf);
    return 1;
}

int appendLastModified(char **request, char *fixed_path){ //add to the response the last modified time. gets the request and path.. if error return -1
    int length = strlen(*request) + 1;
    struct stat fs;
    if(stat(fixed_path, &fs) == -1){return -1;}
    char lastMod[128];
    strftime(lastMod, sizeof(lastMod), RFC1123FMT, gmtime((long *)&fs.st_mtim));
    length += strlen(lastMod);
    *request = (char *)realloc(*request, length);
    strcat(*request, lastMod);
    return 1;
}

void error(int errorNum, char* msg){ //print an error in case of error. gets the Error num (1 for regular, 2 for system call), and msg.
    if(errorNum == 1){
        fprintf(stderr, "Usage: server <port> <poolsize>\n");
        exit(1);
    }
    else if(errorNum == 2){
        perror(msg);
    }
}

void readFile(int fd, char *path){ //read file. gets fd and path.
    FILE *file = fopen(path, "r");
    int read_status = 1;
    unsigned char *buf = calloc(128,sizeof(char));
    while (read_status != 0){
        read_status = fread(buf, sizeof(unsigned char), 128, file);
        write(fd, buf, 128);
        for (int i = 0; i < 128; i++){
            buf[i]= '\0';
        } 
    }
    write(fd, "\r\n\r\n", 4);
    free(buf);
    fclose(file);
}

char* reallocAndConcat(char* res, char* toConcat){ //realloc for the new request and concat. gets res and toConcat.return res concat with to concat.
    char* toReturn;
    int size = strlen(res) + strlen(toConcat) + 1;
    res = (char*)realloc(res, size);
    if(res == NULL){
        return NULL;
    }
    toReturn = strcat(res, toConcat);
    return toReturn;
}

int checkPermission(char* fileName){ //check if the file and the path have premission, if yes return 1, else 0. gets fileName.
    struct stat fs;
    char *temp = (char*)calloc(4000,sizeof(char));
    char *temp2 = (char*)calloc(4000,sizeof(char));
    strcpy(temp, fileName);
    char* token = strtok(temp, "/");
    strcpy(temp2,token);
    strcat(temp2,"/");
    while(token != NULL){
        token = strtok(NULL, "/");
        if(token == NULL){
            if(stat(temp2, &fs) == -1){
                free(temp);
                free(temp2);
                return -1;
            }
            if((S_IXOTH & fs.st_mode) == 0){
                free(temp);
                free(temp2);
                return 0;
            }
        }
        else{
            if(stat(temp2, &fs) == -1){
                free(temp);
                free(temp2);
                return -1;
            }
            if((S_IROTH & fs.st_mode) == 0){
                free(temp);
                free(temp2);
                return 0;
            }
            if(S_ISDIR(fs.st_mode) != 0 && strlen(temp2) > 2){
                strcat(temp2,"/");
            }
            strcat(temp2,token);
        }
    }
    free(temp);
    free(temp2);
    return 1;
}
char *get_mime_type(char *name){    //return the type of the file. gets name.
    char *ext = strrchr(name, '.');
    if (!ext) return NULL;
    if (strcmp(ext, ".html") == 0 || strcmp(ext, ".htm") == 0) return "text/html";
    if (strcmp(ext, ".jpg") == 0 || strcmp(ext, ".jpeg") == 0) return "image/jpeg";
    if (strcmp(ext, ".gif") == 0) return "image/gif";
    if (strcmp(ext, ".png") == 0) return "image/png";
    if (strcmp(ext, ".css") == 0) return "text/css";
    if (strcmp(ext, ".au") == 0) return "audio/basic";
    if (strcmp(ext, ".wav") == 0) return "audio/wav";
    if (strcmp(ext, ".avi") == 0) return "video/x-msvideo";
    if (strcmp(ext, ".mpeg") == 0 || strcmp(ext, ".mpg") == 0) return "video/mpeg";
    if (strcmp(ext, ".mp3") == 0) return "audio/mpeg";
    return NULL;
}

// 	1. 400 - If the it is bad request.
// 	2. 501 - If this is not GET method.
// 	3. 404 - If file not found.
// 	4. 302 - If the path is Dir but has no Slash at the end.
// 	5. 403 - If there is no premissin.
// 	6. 500 - If there was an Error while the server is running.
// 	7. Dir content - If the path is Dir and end with slash.
// 	8. return the file if the file is REG and has no slash.

char* response(int fd, int responseNum, char* version, char* path){ //return the response we need to send. gets fd, responseNum, version and path.
    if(version != NULL){
        version[9] = '\0';
        version[8] = '\0';
    }
    char* toReturn = (char*)calloc(1, sizeof(char));
    if(toReturn == NULL){
        return response(fd, 500, version, path);
    }
    if(responseNum == 400){
        toReturn = reallocAndConcat(toReturn, "HTTP/1.1 400 Bad Request\r\nServer: webserver/1.0\r\nDate: ");
        if(toReturn == NULL){ free(toReturn); return response(fd, 500, version, path);} 
        time_t now;
        char timebuf[128];
        now = time(NULL);
        strftime(timebuf, sizeof(timebuf), RFC1123FMT, gmtime(&now));
        toReturn = reallocAndConcat(toReturn, timebuf);
        if(toReturn == NULL){ free(toReturn); return response(fd, 500, version, path);} 
        toReturn = reallocAndConcat(toReturn, "\r\nContent-Type: text/html\r\nContent-Length: 113\r\nConnection: close\r\n\r\n<HTML><HEAD><TITLE>400 Bad Request</TITLE></HEAD>\r\n<BODY><H4>400 Bad request</H4>\r\nBad Request.\r\n</BODY></HTML>\r\n");
        if(toReturn == NULL){ free(toReturn); return response(fd, 500, version, path);} 
        return toReturn;
    }
    else if(responseNum == 501){
        toReturn = reallocAndConcat(toReturn, "HTTP/1.1 501 Not supported\r\nServer: webserver/1.0\r\nDate: ");
        if(toReturn == NULL){ free(toReturn); return response(fd, 500, version, path);} 
        time_t now;
        char timebuf[128];
        now = time(NULL);
        strftime(timebuf, sizeof(timebuf), RFC1123FMT, gmtime(&now));
        toReturn = reallocAndConcat(toReturn, timebuf);
        if(toReturn == NULL){ free(toReturn); return response(fd, 500, version, path);} 
        toReturn = reallocAndConcat(toReturn, "\r\nContent-Type: text/html\r\nContent-Length: 129\r\nConnection: close\r\n\r\n<HTML><HEAD><TITLE>501 Not supported</TITLE></HEAD>\r\n<BODY><H4>501 Not supported</H4>\r\nMethod is not supported.\r\n</BODY></HTML>\r\n");
        if(toReturn == NULL){ free(toReturn); return response(fd, 500, version, path);} 
        return toReturn;
    }
    else if(responseNum == 404){
        toReturn = reallocAndConcat(toReturn,"HTTP/1.1 404 Not Found\r\nServer: webserver/1.0\r\nDate: ");
        if(toReturn == NULL){ free(toReturn); return response(fd, 500, version, path);} 
        time_t now;
        char timebuf[128];
        now = time(NULL);
        strftime(timebuf, sizeof(timebuf), RFC1123FMT, gmtime(&now));
        toReturn = reallocAndConcat(toReturn, timebuf);
        if(toReturn == NULL){ free(toReturn); return response(fd, 500, version, path);} 
        toReturn = reallocAndConcat(toReturn, "\r\nContent-Type: text/html\r\nContent-Length: 112\r\nConnection: close\r\n\r\n<HTML><HEAD><TITLE>404 Not Found</TITLE></HEAD>\r\n<BODY><H4>404 Not Found</H4>\r\nFile not found.\r\n</BODY></HTML>\r\n");
        if(toReturn == NULL){ free(toReturn); return response(fd, 500, version, path);} 
        return toReturn;
    }
    else if(responseNum == 302){
        toReturn = reallocAndConcat(toReturn, "HTTP/1.1 302 Found\r\nServer: webserver/1.0\r\nDate: ");
        if(toReturn == NULL){ free(toReturn); return response(fd, 500, version, path);} 
        time_t now;
        char timebuf[128];
        now = time(NULL);
        strftime(timebuf, sizeof(timebuf), RFC1123FMT, gmtime(&now));
        toReturn = reallocAndConcat(toReturn, timebuf);
        if(toReturn == NULL){ free(toReturn); return response(fd, 500, version, path);} 
        toReturn = reallocAndConcat(toReturn, "\r\nLocation: ");
        if(toReturn == NULL){ free(toReturn); return response(fd, 500, version, path);} 
        char fileName[4000];
        int idx = 0;
        for(int i =0, j = 1; j < strlen(path); i++, j++){
            fileName[i] = path[j];
            idx++;
        }
        fileName[idx] = '\0';
        toReturn = reallocAndConcat(toReturn, fileName);
        if(toReturn == NULL){ free(toReturn); return response(fd, 500, version, path);} 
        toReturn = reallocAndConcat(toReturn, "/\r\nContent-Type: text/html\r\nContent-Length: 123\r\nConnection: close\r\n\r\n<HTML><HEAD><TITLE>302 Found</TITLE></HEAD>\r\n<BODY><H4>302 Found</H4>\r\nDirectories must end with a slash.\r\n</BODY></HTML>\r\n");
        if(toReturn == NULL){ free(toReturn); return response(fd, 500, version, path);} 
        return toReturn;
    }
    else if(responseNum == 403){
        toReturn = reallocAndConcat(toReturn,"HTTP/1.1 403 Forbidden\r\nServer: webserver/1.0\r\nDate: ");
        if(toReturn == NULL){ free(toReturn); return response(fd, 500, version, path);} 
        time_t now;
        char timebuf[128];
        now = time(NULL);
        strftime(timebuf, sizeof(timebuf), RFC1123FMT, gmtime(&now));
        toReturn = reallocAndConcat(toReturn, timebuf);
        if(toReturn == NULL){ free(toReturn); return response(fd, 500, version, path);} 
        toReturn = reallocAndConcat(toReturn, "\r\nContent-Type: text/html\r\nContent-Length: 111\r\nConnection: close\r\n\r\n<HTML><HEAD><TITLE>403 Forbidden</TITLE></HEAD>\r\n<BODY><H4>403 Forbidden</H4>\r\nAccess denied.\r\n</BODY></HTML>\r\n");
        if(toReturn == NULL){ free(toReturn); return response(fd, 500, version, path);} 
        return toReturn;
    }
    else if(responseNum == 500){
        toReturn = reallocAndConcat(toReturn,"HTTP/1.1 500 Internal Server Error\r\nServer: webserver/1.0\r\nDate: ");
        if(toReturn == NULL){ free(toReturn); error(2, "malloc failed");} 
        time_t now;
        char timebuf[128];
        now = time(NULL);
        strftime(timebuf, sizeof(timebuf), RFC1123FMT, gmtime(&now));
        toReturn = reallocAndConcat(toReturn, timebuf);
        if(toReturn == NULL){ free(toReturn); error(2, "malloc failed");} 
        toReturn = reallocAndConcat(toReturn, "\r\nContent-Type: text/html\r\nContent-Length: 144\r\nConnection: close\r\n\r\n<HTML><HEAD><TITLE>500 Internal Server Error</TITLE></HEAD>\r\n<BODY><H4>500 Internal Server Error</H4>\r\nSome server side error.\r\n</BODY></HTML>\r\n");
        if(toReturn == NULL){ free(toReturn); error(2, "malloc failed");}  
        return toReturn;
    }
    else if(responseNum == 999){
        int check = checkPermission(path);
        if(check == 0){ free(toReturn); return response(fd, 403, version, path);}
        else if(check == -1){ free(toReturn); return response(fd, 500, version, path);}
        char *htmlRes = calloc(1, sizeof(char));
        if(htmlRes == NULL){ free(toReturn); return response(fd, 500, version, path);} 
        htmlRes = reallocAndConcat(htmlRes, "<HTML>\r\n<HEAD><TITLE>Index of ");
        if(htmlRes == NULL){ free(toReturn); return response(fd, 500, version, path);} 
        htmlRes = reallocAndConcat(htmlRes, path);
        if(htmlRes == NULL){ free(toReturn); return response(fd, 500, version, path);} 
        htmlRes = reallocAndConcat(htmlRes, "</TITLE></HEAD>\r\n\r\n<BODY>\r\n<H4>Index of ");
        if(htmlRes == NULL){ free(toReturn); return response(fd, 500, version, path);} 
        htmlRes = reallocAndConcat(htmlRes, path);
        if(htmlRes == NULL){ free(toReturn); return response(fd, 500, version, path);} 
        htmlRes = reallocAndConcat(htmlRes, "</H4>\r\n\r\n<table CELLSPACING=8>\r\n<tr><th>Name</th><th>Last Modified</th><th>Size</th></tr>\r\n");
        if(htmlRes == NULL){ free(toReturn); return response(fd, 500, version, path);} 
        char fileName[4000];
        strcpy(fileName, path);
        DIR* folder = opendir(fileName);
        if(folder == NULL){ free(htmlRes); free(toReturn); return response(fd, 500, version, path);}
        struct dirent* dentry; 
        while ((dentry = readdir(folder)) != NULL){
            htmlRes = reallocAndConcat(htmlRes, "<tr>\r\n<td><A HREF=\"");
            if(htmlRes == NULL){ closedir(folder); free(toReturn); return response(fd, 500, version, path);} 
            htmlRes = reallocAndConcat(htmlRes, dentry->d_name);
            if(htmlRes == NULL){ closedir(folder); free(toReturn); return response(fd, 500, version, path);} 
            htmlRes = reallocAndConcat(htmlRes, "\">");
            if(htmlRes == NULL){ closedir(folder); free(toReturn); return response(fd, 500, version, path);} 
            htmlRes = reallocAndConcat(htmlRes, dentry->d_name);
            if(htmlRes == NULL){ closedir(folder); free(toReturn); return response(fd, 500, version, path);} 
            htmlRes = reallocAndConcat(htmlRes, "</A></td><td>");
            if(htmlRes == NULL){ closedir(folder); free(toReturn); return response(fd, 500, version, path);} 
            struct stat fs;
            char *temp = (char*)calloc(4000,sizeof(char));
            strcpy(temp, fileName);
            strcat(temp, dentry->d_name);
            if(stat(temp, &fs) == -1){ continue;} 
            free(temp);
            char timebuff[128];
            strftime(timebuff, sizeof(timebuff), RFC1123FMT, gmtime(&fs.st_mtim.tv_sec));
            htmlRes = reallocAndConcat(htmlRes, timebuff);
            if(htmlRes == NULL){ closedir(folder); free(toReturn); return response(fd, 500, version, path);} 
            htmlRes = reallocAndConcat(htmlRes, "</td>\r\n");
            if(htmlRes == NULL){ closedir(folder); free(toReturn); return response(fd, 500, version, path);} 
            if(S_ISREG(fs.st_mode)){
                htmlRes = reallocAndConcat(htmlRes, "<td>");
                if(htmlRes == NULL){ closedir(folder); free(toReturn); return response(fd, 500, version, path);} 
                char lenght[10];
                int x = fs.st_size;
                sprintf(lenght,"%d", x);
                htmlRes = reallocAndConcat(htmlRes, lenght);
                if(htmlRes == NULL){ closedir(folder); free(toReturn); return response(fd, 500, version, path);} 
                htmlRes = reallocAndConcat(htmlRes, "</td>\r\n");
                if(htmlRes == NULL){ closedir(folder); free(toReturn); return response(fd, 500, version, path);} 
            }
            else{
                htmlRes = reallocAndConcat(htmlRes, "<td></td>\r\n");
                if(htmlRes == NULL){ closedir(folder); free(toReturn); return response(fd, 500, version, path);} 
            }
            htmlRes = reallocAndConcat(htmlRes, "</tr>\r\n");
            if(htmlRes == NULL){ closedir(folder); free(toReturn); return response(fd, 500, version, path);} 
        }
        htmlRes = reallocAndConcat(htmlRes, "\r\n</table>\r\n<HR>\r\n<ADDRESS>webserver/1.0</ADDRESS>\r\n</BODY></HTML>");
        if(htmlRes == NULL){ closedir(folder); free(toReturn); return response(fd, 500, version, path);} 
        toReturn = reallocAndConcat(toReturn, "HTTP/1.1 200 OK\r\nServer: webserver/1.0\r\nDate: ");
        if(toReturn == NULL){ closedir(folder); free(htmlRes); return response(fd, 500, version, path);} 
        struct stat fs1;
        if(stat(path, &fs1) == -1){ closedir(folder); free(htmlRes); free(toReturn); return response(fd, 500, version, path);} 
        char timebuff[128];
        strftime(timebuff, sizeof(timebuff), RFC1123FMT, gmtime(&fs1.st_mtim.tv_sec));
        time_t now;
        char timebuf[128];
        now = time(NULL);
        strftime(timebuf, sizeof(timebuf), RFC1123FMT, gmtime(&now));
        toReturn = reallocAndConcat(toReturn, timebuf);
        if(toReturn == NULL){ closedir(folder); free(htmlRes); return response(fd, 500, version, path);} 
        toReturn = reallocAndConcat(toReturn, "\r\nContent-Type: text/html\r\nContent-Length: ");//\r\nConnection: close\r\n\r\n<HTML><HEAD><TITLE>404 Not Found</TITLE></HEAD>\r\n<BODY><H4>404 Not Found</H4>\r\nFile not found.\r\n</BODY></HTML>\r\n");
        if(toReturn == NULL){ closedir(folder); free(htmlRes); return response(fd, 500, version, path);} 
        char buffer2[15];
        sprintf(buffer2, "%d", (int)strlen(htmlRes));
        toReturn = reallocAndConcat(toReturn, buffer2);
        toReturn = reallocAndConcat(toReturn, "\r\nLast-Modified: ");
        if(toReturn == NULL){ closedir(folder); free(htmlRes); return response(fd, 500, version, path);} 
        struct stat fs;
        if(stat(path, &fs) == -1){ closedir(folder); free(htmlRes); free(toReturn); return response(fd, 500, version, path);} 
        char lastmod[128];
        strftime(lastmod, sizeof(lastmod), RFC1123FMT, gmtime(&fs.st_mtime));
        toReturn = reallocAndConcat(toReturn, lastmod);
        if(toReturn == NULL){ closedir(folder); free(htmlRes); return response(fd, 500, version, path);} 
        toReturn = reallocAndConcat(toReturn, "\r\nConnection: close\r\n\r\n");
        if(toReturn == NULL){ closedir(folder); free(htmlRes); return response(fd, 500, version, path);} 
        toReturn = reallocAndConcat(toReturn, htmlRes);
        if(toReturn == NULL){ closedir(folder); free(htmlRes); return response(fd, 500, version, path);} 
        closedir(folder);
        free(htmlRes);
        return toReturn;
    }
    else if(responseNum == 100){
        int check = checkPermission(path);
        if(check == 0){ free(toReturn); return response(fd, 403, version, path);}
        struct stat fs1;
        if(stat(path, &fs1) == -1){ free(toReturn); return response(fd, 500, version, path);} 
        char fileName[4000];
        int idx = strlen(path) - 1;
        for(int i = strlen(path) - 1; i >= 0; i--){
            if(path[i] == '/'){
                break;
            }
            idx--;
        }
        int fileNameIdx = 0;
        for(int i = idx + 1; i < strlen(path); i++){
            fileName[fileNameIdx] = path[i];
            fileNameIdx++;
        }
        fileName[fileNameIdx] = '\0';
        struct stat fs;
        if(stat(path, &fs) == -1){free(toReturn); 
        return response(fd, 500, version, path);} 
        char size_bytes[15];
        sprintf(size_bytes, "%d", (int)fs.st_size);
        char type[20];
        if(get_mime_type(fileName) != NULL) strcpy(type,get_mime_type(fileName));
        else type[0] = 0;
        toReturn = reallocAndConcat(toReturn, "HTTP/1.1 200 OK\r\nServer: webserver/1.0\r\nDate: ");
        if(toReturn == NULL){ return response(fd, 500, version, path);} 
        int x = appendCurrentTime(&toReturn);
        if(type != NULL ){    
            toReturn = reallocAndConcat(toReturn, "\r\nContent-Type: ");
            if(toReturn == NULL){ return response(fd, 500, version, path);} 
            toReturn = reallocAndConcat(toReturn,type);
            if(toReturn == NULL){ return response(fd, 500, version, path);} 
        }   
        toReturn = reallocAndConcat(toReturn, "\r\nContent-Length: ");
        if(toReturn == NULL){ return response(fd, 500, version, path);} 
        toReturn = reallocAndConcat(toReturn,size_bytes);
        if(toReturn == NULL){ return response(fd, 500, version, path);} 
        toReturn = reallocAndConcat(toReturn, "\r\nLast-Modified: ");
        if(toReturn == NULL){ return response(fd, 500, version, path);} 
        x = appendLastModified(&toReturn, path);
        if(x == -1){ free(toReturn); return response(fd, 500, version, path);} 
        toReturn = reallocAndConcat(toReturn, "\r\nConnection: close\r\n\r\n");
        if(toReturn == NULL){ return response(fd, 500, version, path);} 
        return toReturn;
    }
    return toReturn;
}

int validation(char* toCheck, int fd){ //check which response set. gets toCheck and fd.
    char method[4000];
    char path[4000];
    char version[4000];
    char temp[4000];
    char* res;
    int loop = 0;
    strcpy(temp, toCheck);
    char* token = strtok(temp, " ");
    strcpy(method, token);
    while(token != NULL){
        token = strtok(NULL, " ");
        loop++;
        if(loop == 1){
            if(token == NULL){ //BAD REQUEST
                res = response(fd ,400, version, path);
                int w = write(fd, res, strlen(res));
                free(res);
                if(w == -1){
                    error(2, "write failed");
                    return -1;
                }
                return 1; 
            }
            if(token[0] == '.'){ //BAD REQUEST
                res = response(fd, 400, version, path);
                int w = write(fd, res, strlen(res));
                free(res);
                if(w == -1){
                    error(2, "write failed");
                    return -1;
                }
                return 1; 
            }
            path[0] = '.';
            path[1] = '\0';
            strcat(path, token);
        }
        else if(loop == 2){
            if(token == NULL){ //BAD REQUEST
               res = response(fd, 400, version, path);
                int w = write(fd, res, strlen(res));
                free(res);
                if(w == -1){
                    error(2, "write failed");
                    return -1;
                }
                return 1; 
            }
            strcpy(version, token);
        }
    }
    if(loop != 3){ //BAD REQUEST
        res = response(fd, 400, version, path);
        int w = write(fd, res, strlen(res));
        free(res);
        if(w == -1){
            error(2, "write failed");
            return -1;
        }
        return 1;
    }
    if(strcmp(version, "HTTP/1.1\r\n") != 0 && strcmp(version, "HTTP/1.0\r\n") != 0){ //BAD REQUEST
        res = response(fd, 400, version, path);
        int w = write(fd, res, strlen(res));
        free(res);
        if(w == -1){
            error(2, "write failed");
            return -1;
        }
        return 1;
    }
    else if(strstr(path, "//") != NULL){ //BAD REQUEST
        res = response(fd, 400, version, path);
        int w = write(fd, res, strlen(res));
        free(res);
        if(w == -1){
            error(2, "write failed");
            return -1;
        }
        return 1;
    }
    else if(path[1] != '/'){ //BAD REQUEST
        res = response(fd, 400, version, path);
        int w = write(fd, res, strlen(res));
        free(res);
        if(w == -1){
            error(2, "write failed");
            return -1;
        }
        return 1; 
    }
    else if(strcmp(method, "GET") != 0){ //NOT SUPPORTED
        res = response(fd, 501, version, path);
        int w = write(fd, res, strlen(res));
        free(res);
        if(w == -1){
            error(2, "write failed");
            return -1;
        }
        return 1;
    }
    struct stat fs;
    char fileName[4000];
    int isSlash = 0;
    strcpy(fileName,path);
    if(fileName[strlen(fileName)-1] == '/'){
        isSlash = 1;
    }
    if(stat(fileName, &fs) == -1){ //NOT FOUND
        res = response(fd, 404, version, path);
        int w = write(fd, res, strlen(res));
        free(res);
        if(w == -1){
            error(2, "write failed");
            return -1;
        }
        return 1;
    }
    else if (isSlash == 0 && S_ISDIR(fs.st_mode) != 0){  //FOUND BUT NO SLASH
        res = response(fd, 302, version, path);
        int w = write(fd, res, strlen(res));
        free(res);
        if(w == -1){
            error(2, "write failed");
            return -1;
        }
        return 1;
    }
    else if (isSlash == 1 && S_ISDIR(fs.st_mode) != 0){ //FOUND, DIR CONTECT
        strcat(fileName, "index.html");
        if(stat(fileName, &fs) == -1){
            res = response(fd, 999, version, path);
            int w = write(fd, res, strlen(res));
            free(res);
            if(w == -1){
                error(2, "write failed");
                return -1;
            }
            return 1;
        }
        else{ //FOUND, FILE INDEX.HTML
           res = response(fd, 100, version, fileName);
            int w = write(fd, res, strlen(res));
            if(w == -1){
                error(2, "write failed");
                return -1;
            }
            char *c = (char*)calloc(35, sizeof(char));
            strncpy(c, res, 34);
            c[34] = '\0';
            if(checkPermission(path) != 0 && strcmp("HTTP/1.1 500 Internal Server Error", c) != 0){
                readFile(fd, path);
            }
            free(c);
            free(res);
            return 1;
        }
    }
    else if(isSlash == 0 && S_ISREG(fs.st_mode) != 0){ //FOUND, FILE
        res = response(fd, 100, version, path);
        int w = write(fd, res, strlen(res));
        if(w == -1){
            error(2, "write failed");
            return -1;
        }
        char *c = (char*)calloc(35, sizeof(char));
        strncpy(c, res, 34);
        c[34] = '\0';
        if(checkPermission(path) != 0 && strcmp("HTTP/1.1 500 Internal Server Error", c) != 0){
            readFile(fd, path);
        }
        free(c);
        free(res);
        return 1;
    }
    else if(isSlash == 0 && S_ISREG(fs.st_mode) == 0){ //FOUND, FILE BUT NOT REGULAR, FORBIEN
        res = response(fd, 403, version, path);
        int w = write(fd, res, strlen(res));
        free(res);
        if(w == -1){
            error(2, "write failed");
            return -1;
        }
        return 1;
    }
    return 0;
}

int handler(void* arg){ //handle for dispacth. Gets fd as void*.
    char buffer[4000];
    int rc;
    int isR = 0;
    char temp[1];
    temp[0] = '\0';
    int idx = 0;
    while (1){
        rc = read(*(int*)arg, temp, sizeof(temp));
        if(rc == 0 || (isR == 1 && temp[0] == '\n')){
            break;
        }
        else if(rc > 0){
            buffer[idx] = temp[0];
            if(temp[0] == '\r'){
                isR = 1;
            }
            else if(isR == 1 && temp[0] != '\n'){
                isR = 0;
            } 
            idx++; 
        }
        else{
            error(2, "socket failed");
        }
    }
    buffer[idx] = '\n';
    buffer[idx + 1] = '\0';
    int val = validation(buffer, *(int*)arg);
    if(val == -1){
        close(*(int*)arg);
        error(2, "socket failed");
    }
    close(*(int*)arg);
    return *(int*)arg;
}

int main(int argc, char *argv[]) { //the main of the Server. conect to main socket bind listen and accept for new socket each request. and dispath each one.
    if(argc < 4){
        error(1, "Usage: server <port> <poolsize>\n");
    }
    int port = atoi(argv[1]);
    if(port < 0){
        error(1, "Usage: server <port> <poolsize>\n");
    }
    int pool_size = atoi(argv[2]);
    if(pool_size <= 0){
        error(1, "Usage: server <port> <poolsize>\n");
    }
    int max_number_of_request = atoi(argv[3]);
    if(max_number_of_request < 0){
        error(1, "Usage: server <port> <poolsize>\n");
    }
    threadpool *threadPool = create_threadpool(pool_size);
    if(threadPool == NULL){
        error(1, "Usage: server <port> <poolsize>\n");
    }
    int socketfd, newSocketfd;
    struct sockaddr_in server_addr;
    struct sockaddr client_addr;
    socklen_t client = 0;
    socketfd = socket(AF_INET,SOCK_STREAM,0);
    if(socketfd < 0){
        destroy_threadpool(threadPool);
        error(2, "socket failed\n");
        exit(1);
    }
    server_addr.sin_family = AF_INET;
    server_addr.sin_addr.s_addr = htonl(INADDR_ANY);
    server_addr.sin_port = htons(port);
    if(bind(socketfd, (struct  sockaddr *) &server_addr, sizeof(server_addr)) < 0){
        destroy_threadpool(threadPool);
        close(socketfd);
        error(2, "bind failed\n");
        exit(1);
    }
    if(listen(socketfd, max_number_of_request) < 0){
        destroy_threadpool(threadPool);
        close(socketfd);
        error(2, "listen failed\n");
        exit(1);
    }
    for(int i = 0; i < max_number_of_request; i++){
        client = 0;
        newSocketfd = accept(socketfd, &client_addr, &client);
        if(newSocketfd < 0){
            destroy_threadpool(threadPool);
            close(socketfd);
            error(2, "accept failed\n");
            exit(1);
        }
        dispatch(threadPool,&handler, &newSocketfd);
    }
    destroy_threadpool(threadPool);
    close(socketfd);
    return 0;
}