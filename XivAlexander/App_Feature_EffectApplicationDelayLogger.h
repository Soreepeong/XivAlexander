#pragma once
namespace App::Feature {
	class EffectApplicationDelayLogger {
		class Internals;
		std::unique_ptr<Internals> impl;

	public:
		EffectApplicationDelayLogger();
		~EffectApplicationDelayLogger();
	};
}
