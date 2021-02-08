#pragma once
namespace App::Feature {
	class IpcTypeFinder {
		class Internals;
		std::unique_ptr<Internals> impl;

	public:
		IpcTypeFinder();
		~IpcTypeFinder();
	};
}
