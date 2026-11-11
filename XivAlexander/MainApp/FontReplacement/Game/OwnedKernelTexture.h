#pragma once

#include "MainApp/FontReplacement/Utilities/Misc.h"

namespace XivAlexander::Apps::MainApp::FontReplacement {
	class OwnedKernelTexture {
		uintptr_t m_kernel = 0;
		ID3D11ResourcePtr m_resource;

	public:
		OwnedKernelTexture(int width, int height, bool format4444 = false);
		OwnedKernelTexture(const OwnedKernelTexture&) = delete;
		OwnedKernelTexture& operator=(const OwnedKernelTexture&) = delete;
		~OwnedKernelTexture();

		[[nodiscard]] uintptr_t Kernel() const { return m_kernel; }

		[[nodiscard]] ID3D11Resource* Resource() const { return m_resource.GetInterfacePtr(); }
	};
}
