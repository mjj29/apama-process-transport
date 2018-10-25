#include<sag_connectivity_plugins.hpp>
#include<string>
#include<thread>
#include<unistd.h>
#include<stdio.h>
#include <fcntl.h>

using com::softwareag::connectivity::AbstractSimpleTransport;
using com::softwareag::connectivity::MapExtractor;
using com::softwareag::connectivity::Message;
using com::softwareag::connectivity::list_t;
using com::softwareag::connectivity::map_t;
using com::softwareag::connectivity::data_t;
using com::softwareag::connectivity::get;
using com::softwareag::connectivity::convert_to;

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

