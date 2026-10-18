#pragma once

#include "MainApp/FontReplacement/Utilities/Misc.h"

namespace XivAlexander::Apps::MainApp::FontReplacement {
	// A Kernel::Texture on the game's device, B8G8R8A8 or B4G4R4A4; destroying it releases it once the game is done with it.
	class KernelTextureWrapper {
		uintptr_t m_kernel = 0;
		ID3D11ResourcePtr m_resource;
		int m_decRef = 0;

	public:
		KernelTextureWrapper(int width, int height, bool format4444 = false);
		KernelTextureWrapper(const KernelTextureWrapper&) = delete;
		KernelTextureWrapper& operator=(const KernelTextureWrapper&) = delete;
		~KernelTextureWrapper();

		[[nodiscard]] uintptr_t Kernel() const { return m_kernel; }

		[[nodiscard]] ID3D11Resource* Resource() const { return m_resource.GetInterfacePtr(); }
	};
}
