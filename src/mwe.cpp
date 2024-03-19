#include <array>
#include <chrono>
#include <filesystem>
#include <iostream>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <processthreadsapi.h>

static std::mutex mutex;

void PrintMessageLocked(std::string const& s) {
	std::lock_guard<std::mutex> lock(mutex);
	std::cout << s << std::endl;
}

void PrintTimeLocked(char const* what, std::size_t const& threadId, std::size_t const& usTaken) {
	std::lock_guard<std::mutex> lock(mutex);
	std::cout << "Thead " << threadId << ": " << what << " took " << usTaken << "ms." << std::endl;
}

std::tuple<int, std::string> ExecuteProcessAndCaptureOutput(std::string command, std::string workingDirectory, std::size_t const& myThreadId) {
	/*
		Why this whole mumbu-jumbo instead of a simple popen/pclose()?
		Because the maximum length of command line arguments is 8191 characters on Windows, but almost all invocations are longer than that.
	*/
	HANDLE g_hChildStd_OUT_Rd = NULL;
	HANDLE g_hChildStd_OUT_Wr = NULL;

	SECURITY_ATTRIBUTES saAttr;

	// Set the bInheritHandle flag so pipe handles are inherited. 
	saAttr.nLength = sizeof(SECURITY_ATTRIBUTES);
	saAttr.bInheritHandle = TRUE;
	saAttr.lpSecurityDescriptor = NULL;

	// Create a pipe for the child process's STDOUT. 
	DWORD const outputBufferSize = 1024 * 1024;
	if (!CreatePipe(&g_hChildStd_OUT_Rd, &g_hChildStd_OUT_Wr, &saAttr, outputBufferSize)) {
		std::cerr << "Failed to create STDOUT pipe for child process, last error = " << GetLastError() << std::endl;
		throw;
	}

	// Ensure the read handle to the pipe for STDOUT is not inherited.
	if (!SetHandleInformation(g_hChildStd_OUT_Rd, HANDLE_FLAG_INHERIT, 0)) {
		std::cerr << "Failed to set handle information for child process, last error = " << GetLastError() << std::endl;
		throw;
	}

	STARTUPINFOA startupInfo;
	PROCESS_INFORMATION processInformation;
	ZeroMemory(&startupInfo, sizeof(startupInfo));
	startupInfo.cb = sizeof(startupInfo);
	ZeroMemory(&processInformation, sizeof(processInformation));

	startupInfo.hStdError = g_hChildStd_OUT_Wr;
	startupInfo.hStdOutput = g_hChildStd_OUT_Wr;
	startupInfo.dwFlags |= STARTF_USESTDHANDLES;

	BOOL bSuccess = CreateProcessA(NULL, command.data(), NULL, NULL, TRUE, 0, NULL, workingDirectory.data(), &startupInfo, &processInformation);
	if (!bSuccess) {
		std::cerr << "Failed to create process, last error = " << GetLastError() << std::endl;
		throw;
	}

	// Close unnecessary pipes
	CloseHandle(g_hChildStd_OUT_Wr);

	// Wait until child process exits.
	auto const waitStart = std::chrono::steady_clock::now();
	DWORD const timeoutInMs = 30000; // Can be INFINITE
	auto const waitResult = WaitForSingleObject(processInformation.hProcess, timeoutInMs);
	if (waitResult == WAIT_TIMEOUT) {
		std::cerr << "Internal Error: Execution took longer than " << timeoutInMs << "ms, abandoning!" << std::endl;

		std::cerr << "Command was: '" << command << "'" << std::endl;
		auto const terminationResult = TerminateProcess(processInformation.hProcess, -1);
		if (terminationResult == 0) {
			std::cerr << "Internal Error: Failed to terminate sub-process :(" << std::endl;
		}
	}
	auto const waitEnd = std::chrono::steady_clock::now();
	auto const waitTimeTaken = std::chrono::duration_cast<std::chrono::milliseconds>(waitEnd - waitStart).count();
	PrintTimeLocked("Waiting for object", myThreadId, waitTimeTaken);

	DWORD exitCode = 0;
	bSuccess = GetExitCodeProcess(processInformation.hProcess, &exitCode);
	if (bSuccess == 0) {
		std::cerr << "Internal Error: Failed to get exit code from process!" << std::endl;
		throw;
	}
	else if (exitCode == STILL_ACTIVE) {
		std::cerr << "Internal Error: Failed to get exit code from process, it is still running!" << std::endl;
		throw;
	}

	// Close process and thread handles. 
	CloseHandle(processInformation.hProcess);
	CloseHandle(processInformation.hThread);

	// Collect output from process
	auto const outputStart = std::chrono::steady_clock::now();
	constexpr DWORD BUFFER_SIZE = 4096;
	std::array<char, BUFFER_SIZE> buffer;
	std::string resultString;
	DWORD dwRead = 0;
#ifdef PEEK_FIRST
	DWORD dwTotalBytesAvailable = 0;
#endif
	try {
		do {
#ifdef PEEK_FIRST
			// ReadFile can block the entire process instead of just this thread, so we need to peek first to make sure we only read what is there
			bSuccess = PeekNamedPipe(g_hChildStd_OUT_Rd, NULL, 0, NULL, &dwTotalBytesAvailable, NULL);
			if (!bSuccess || dwTotalBytesAvailable == 0) break;

			bSuccess = ReadFile(g_hChildStd_OUT_Rd, buffer.data(), std::min(BUFFER_SIZE, dwTotalBytesAvailable), &dwRead, NULL);
			if (!bSuccess || dwRead == 0) break;
#else
			using namespace std::chrono_literals;
			std::this_thread::sleep_for(500ms);
			PrintTimeLocked("Now in front of ReadFile", myThreadId, 0);
			std::this_thread::sleep_for(100ms);

			bSuccess = ReadFile(g_hChildStd_OUT_Rd, buffer.data(), BUFFER_SIZE, &dwRead, NULL);
			if (!bSuccess || dwRead == 0) break;
			PrintTimeLocked("ReadFile returned", myThreadId, dwRead);
#endif
			resultString.append(buffer.data(), dwRead);
		} while (true);
	}
	catch (...) {
		std::cerr << "Internal Error: Encountered an exception while reading process output!" << std::endl;
		throw;
	}
	auto const outputEnd = std::chrono::steady_clock::now();
	auto const outputTimeTaken = std::chrono::duration_cast<std::chrono::milliseconds>(outputEnd - outputStart).count();
	PrintTimeLocked("Reading output", myThreadId, outputTimeTaken);

	// And close the handle to reading STDOUT
	CloseHandle(g_hChildStd_OUT_Rd);

	return std::make_tuple(static_cast<int>(exitCode), resultString);
}



void ThreadFunction(std::vector<std::size_t> const& times, std::size_t const myThreadId) {
	std::size_t const maxTimes = times.size();
	static std::atomic<std::size_t> workCounter = 0;
	while (true) {
		std::size_t const currentTimeIndex = workCounter++;
		if (currentTimeIndex >= maxTimes) {
			break;
		}
		auto const& myTime = times.at(currentTimeIndex);

		auto const procStart = std::chrono::steady_clock::now();
		auto const [res, output] = ExecuteProcessAndCaptureOutput(".\\sleepHelper.exe " + std::to_string(myTime), std::filesystem::current_path().string(), myThreadId);
		auto const procEnd = std::chrono::steady_clock::now();
		auto const procTimeTaken = std::chrono::duration_cast<std::chrono::milliseconds>(procEnd - procStart).count();
		PrintTimeLocked("Executing process", myThreadId, procTimeTaken);
	}

	{
		std::lock_guard<std::mutex> lock(mutex);
		std::cout << "Thread " << myThreadId << " is exiting." << std::endl;
	}
}

int main(int argc, char** argv) {
	std::size_t threadCount = 3;
	if (argc > 1) {
		threadCount = std::stoull(argv[1]);
	}
	if (threadCount < 1 || threadCount > 64) {
		std::cerr << "Thread count out of bounds. Use 1 <= thread count <= 64." << std::endl;
		return -1;
	}
	std::cout << "Using thread count = " << threadCount << std::endl;



	std::vector<std::size_t> times = {
		5000,
	};
	for (std::size_t i = 1; i < threadCount; ++i) {
		times.push_back(500);
	}

	auto timeStart = std::chrono::steady_clock::now();
	std::vector<std::thread> threads;
	for (std::size_t i = 0; i < threadCount; ++i) {
		threads.emplace_back(&ThreadFunction, times, i + 1);
	}
	for (auto& t : threads) {
		t.join();
	}

	auto timeEnd = std::chrono::steady_clock::now();
	std::cout << "Total runtime was " << std::chrono::duration_cast<std::chrono::milliseconds>(timeEnd - timeStart).count() << "ms." << std::endl;

	return 0;
}
