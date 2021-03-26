#pragma once
namespace App::Feature {
	class AllIpcMessageLogger {
		class Internals;
		std::unique_ptr<Internals> impl;

	public:
		AllIpcMessageLogger();
		~AllIpcMessageLogger();
	};
}
