#include<cinttypes>
#include<sag_connectivity_plugins.hpp>
#include<string>
#include<thread>
#include<stdio.h>
#include<fcntl.h>

#ifdef _WIN32

#include <windows.h> 
#include <tchar.h>
#include <strsafe.h>
#define BUFSIZE 4096

#else

#include <unistd.h>

#endif

using com::softwareag::connectivity::AbstractSimpleTransport;
using com::softwareag::connectivity::MapExtractor;
using com::softwareag::connectivity::Message;
using com::softwareag::connectivity::list_t;
using com::softwareag::connectivity::map_t;
using com::softwareag::connectivity::data_t;
using com::softwareag::connectivity::get;
using com::softwareag::connectivity::convert_to;

using namespace com::softwareag::connectivity;

namespace com { namespace apamax {

class ProcessTransport: public AbstractSimpleTransport
{
public:
	explicit ProcessTransport(const TransportConstructorParameters &param)
		: AbstractSimpleTransport(param)
	{
		MapExtractor configEx(config, "config");
		command = configEx.get<list_t>("command").copy();
		if (command.size() < 1) throw std::runtime_error("Command must contain at least one element");
		configEx.checkNoItemsRemaining();
		logger.info("Configured Process transport to execute command %s", "");
	}
	virtual void hostReady() override
	{
		t = std::thread(runProcess, this);
	}
	virtual void shutdown() override
	{
		killProcess();
		t.join();
	}
	virtual void deliverMessageTowardsTransport(Message &m) override
	{
		// do nothing
	}
private:
	static void runProcess(ProcessTransport *transport)
	{
#ifdef _WIN32
		HANDLE g_hChildStd_OUT_Rd = NULL;
		HANDLE g_hChildStd_OUT_Wr = NULL;
		SECURITY_ATTRIBUTES saAttr; 
		saAttr.nLength = sizeof(SECURITY_ATTRIBUTES); 
		saAttr.bInheritHandle = TRUE; 
		saAttr.lpSecurityDescriptor = NULL; 

		// Create a pipe for the child process's STDOUT. 
		if ( ! CreatePipe(&g_hChildStd_OUT_Rd, &g_hChildStd_OUT_Wr, &saAttr, 0) ) {
			transport->logger.error("Failed to create subprocess pipe"); 
			return;
		}

		// Ensure the read handle to the pipe for STDOUT is not inherited.
		if ( ! SetHandleInformation(g_hChildStd_OUT_Rd, HANDLE_FLAG_INHERIT, 0) ) {
			transport->logger.error("Failed to create subprocess pipe"); 
			return;
		}

		std::ostringstream oss;
		for (auto it = transport->command.begin(); it != transport->command.end(); ++it) {
			oss << '"' << get<const char*>(*it) << "\" ";
		}

		PROCESS_INFORMATION piProcInfo; 
		STARTUPINFO siStartInfo;
		BOOL bSuccess = false; 
 
		ZeroMemory( &piProcInfo, sizeof(PROCESS_INFORMATION) );
		ZeroMemory( &siStartInfo, sizeof(STARTUPINFO) );
		siStartInfo.cb = sizeof(STARTUPINFO); 
		siStartInfo.hStdError = g_hChildStd_OUT_Wr;
		siStartInfo.hStdOutput = g_hChildStd_OUT_Wr;
		siStartInfo.dwFlags |= STARTF_USESTDHANDLES;
 
// Create the child process. 
    
		bSuccess = CreateProcessA(get<const char*>(transport->command[0]), 
				const_cast<char*>(oss.str().c_str()),     // command line 
				nullptr,       // process security attributes 
				nullptr,       // primary thread security attributes 
				true,          // handles are inherited 
				0,             // creation flags 
				nullptr,       // use parent's environment 
				nullptr,       // use parent's current directory 
				&siStartInfo,  // STARTUPINFO pointer 
				&piProcInfo);  // receives PROCESS_INFORMATION 

		// If an error occurs, exit the application. 
		if ( ! bSuccess ) {
			transport->logger.error("Failed to create sub-process");
			return;
		} else {
			CloseHandle(piProcInfo.hProcess);
			CloseHandle(piProcInfo.hThread);
		}

		CHAR chBuf[BUFSIZE]; 
		char *r;
		HANDLE hParentStdOut = GetStdHandle(STD_OUTPUT_HANDLE);
		FILE *fp = fdopen(_open_osfhandle((intptr_t)g_hChildStd_OUT_Rd, _O_RDONLY), "rb");

		while ((r = fgets(chBuf, BUFSIZE, fp)) || errno==EAGAIN )
		{ 
			Message m{data_t{chBuf}};
			transport->hostSide->sendBatchTowardsHost(&m, &m+1);
		} 
#else
		int pipeForStdOut[2];
		pipe(pipeForStdOut);
		transport->childPid = fork();
		if (transport->childPid == -1) {
			// fork error
			transport->logger.error("Failed to fork for executing command");
			return;
		} else if (transport->childPid == 0) {
			// child process
			close(pipeForStdOut[0]);
			dup2(pipeForStdOut[1], 1);
			std::unique_ptr<const char*[]> args(new const char*[transport->command.size()+1]);
			size_t i = 0;
			for (auto it = transport->command.begin(); it != transport->command.end(); ++it) {
				args[i++] = get<const char*>(*it);
			}
			args[i] = nullptr;
			execv(get<const char*>(transport->command[0]), const_cast<char* const*>(args.get()));
		} else {
			transport->logger.info("Created process with pid %" PRIu64, transport->childPid);
			// parent process
			auto f = fdopen(pipeForStdOut[0], "rb");
			char *line = nullptr;
			size_t len = 0;
			ssize_t read;
			while (!transport->stopped && ((read = getline(&line, &len, f)) != -1 || errno == EAGAIN)) {
				if (read > 0) {
					Message m{data_t{line}};
					transport->hostSide->sendBatchTowardsHost(&m, &m+1);
				}
			}
			free(line);
			close(pipeForStdOut[0]);
			transport->logger.info("Transport finished reading");
		}
#endif
	}
	void killProcess()
	{
		stopped=true;
	}
	list_t command;
	std::thread t;
	bool stopped=false;
	pid_t childPid=-1;
};

SAG_DECLARE_CONNECTIVITY_TRANSPORT_CLASS(ProcessTransport)

}} // com::apamax

