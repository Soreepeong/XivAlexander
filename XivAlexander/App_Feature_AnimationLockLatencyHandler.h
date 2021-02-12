#pragma once
namespace App::Feature {
	class AnimationLockLatencyHandler {
		class Internals;
		std::unique_ptr<Internals> impl;

	public:
		AnimationLockLatencyHandler();
		~AnimationLockLatencyHandler();
	};
}
