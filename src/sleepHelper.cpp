#include <chrono>
#include <iostream>
#include <string>
#include <thread>

int main(int argc, char** argv) {
	if (argc <= 1) {
		std::cerr << "Error: Expected an argument." << std::endl;
		return -1;
	}
	std::string const arg(argv[1]);
	auto const time = std::stoull(arg);

	std::cout << "Going to sleep for " << time << " ms." << std::endl;

	auto const sleepStart = std::chrono::steady_clock::now();
	const std::chrono::duration<std::size_t, std::milli> duration(time);
	std::this_thread::sleep_for(duration);
	auto const sleepEnd = std::chrono::steady_clock::now();
	auto const timeSlept = std::chrono::duration_cast<std::chrono::microseconds>(sleepEnd - sleepStart).count();
	std::cout << "Slept for " << timeSlept << "us." << std::endl;

	return 0;
}
