# Server

===Description ===

Program files:

server.c - server that handle request from client from browser or telnet. Gets from user port, num of max tread and num of max request. The server send the next following responses: 
	1. 400 - If the it is bad request.
	2. 501 - If this is not GET method.
	3. 404 - If file not found.
	4. 302 - If the path is Dir but has no Slash at the end.
	5. 403 - If there is no premissin.
	6. 500 - If there was an Error while the server is running.
	7. Dir content - If the path is Dir and end with slash.
	8. return the file if the file is REG and has no slash.
In any case of Dir content or file returned the following response will be 200 OK.
	
funcation:

	int appendCurrentTime(char **request) - add to the response the current time. gets the request.
	int appendLastModified(char **request, char *fixed_path) - add to the response the last modified time. gets the request and path.
	void error(int errorNum, char* msg) - print an error in case of error. gets the Error num (1 for regular, 2 for system call), and msg.
	void readFile(int fd, char *path) - read file. gets fd and path.
	char* reallocAndConcat(char* res, char* toConcat) - realloc for the new request and concat. gets res and toConcat.
	int checkPermission(char* fileName) - check if the file and the path have premission, if yes return 1, else 0. gets fileName.
	char *get_mime_type(char *name) - return the type of the file. gets name.
	char* response(int fd, int responseNum, char* version, char* path) - return the response we need to send. gets fd, responseNum, version and path.
	int validation(char* toCheck, int fd) - check which response set. gets toCheck and fd.
	int handler(void* arg) - handle for dispacth. Gets fd as void*.
	int main(int argc, char *argv[]) - the main of the Server. conect to main socket bind listen and accept for new socket each request. and dispath each one.


threadpool.c - threadpool is alot of threads that wait for an apply to make, until then all the threads sleep.

funcation:

	threadpool* create_threadpool(int num_threads_in_pool) - create the treadpool, gets num_threads_in_pool.
	void dispatch(threadpool* threadPool, dispatch_fn disPatch, void *arg) - add an apply to the queue and wake up threads for make it. gets threadPool, disPatch and fd as void*.
	void* do_work(void* threadPool) - calling to the handler func for the apply.
	void destroy_threadpool(threadpool* threadPool) - destroy the threadpool and free all. gets threadpool.
