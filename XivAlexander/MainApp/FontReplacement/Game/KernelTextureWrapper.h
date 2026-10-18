#pragma once

#include "MainApp/FontReplacement/Utilities/Misc.h"

namespace XivAlexander::Apps::MainApp::FontReplacement {
	class KernelTextureWrapper {
		uintptr_t m_kernel = 0;
		ID3D11ResourcePtr m_resource;

	public:
		KernelTextureWrapper(int width, int height, bool format4444 = false);
		KernelTextureWrapper(const KernelTextureWrapper&) = delete;
		KernelTextureWrapper& operator=(const KernelTextureWrapper&) = delete;
		~KernelTextureWrapper();

		[[nodiscard]] uintptr_t Kernel() const { return m_kernel; }

		[[nodiscard]] ID3D11Resource* Resource() const { return m_resource.GetInterfacePtr(); }
	};
}
