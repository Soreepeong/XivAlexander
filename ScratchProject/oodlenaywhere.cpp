#include <vector>

#define NOMINMAX
#define WIN32_LEAN_AND_MEAN
#include <Windows.h>

using OodleNetwork1_Shared_Size = int(__stdcall)(int htbits);
using OodleNetwork1_Shared_SetWindow = void(__stdcall)(void* data, int htbits, void* window, int windowSize);
using OodleNetwork1UDP_Train = void(__stdcall)(void* state, void* shared, const void* const* trainingPacketPointers, const int* trainingPacketSizes, int trainingPacketCount);
using OodleNetwork1UDP_Decode = bool(__stdcall)(void* state, void* shared, const void* compressed, size_t compressedSize, void* raw, size_t rawSize);
using OodleNetwork1UDP_Encode = int(__stdcall)(const void* state, const void* shared, const void* raw, size_t rawSize, void* compressed);
using OodleNetwork1UDP_State_Size = int(__stdcall)(void);

#ifdef _WIN64
const auto GamePath = LR"(C:\Program Files (x86)\SquareEnix\FINAL FANTASY XIV - A Realm Reborn\game\ffxiv_dx11.exe)";
constexpr size_t off_OodleMalloc = 0x1f21cf8;
constexpr size_t off_OodleFree = 0x1f21d00;
constexpr size_t off_OodleNetwork1_Shared_Size = 0x153edf0;
constexpr size_t off_OodleNetwork1_Shared_SetWindow = 0x153ecc0;
constexpr size_t off_OodleNetwork1UDP_Train = 0x153d920;
constexpr size_t off_OodleNetwork1UDP_Decode = 0x153cdd0;
constexpr size_t off_OodleNetwork1UDP_Encode = 0x153ce20;
constexpr size_t off_OodleNetwork1UDP_State_Size = 0x153d470;
#else
const auto GamePath = LR"(C:\Program Files (x86)\SquareEnix\FINAL FANTASY XIV - A Realm Reborn\game\ffxiv.exe)";
constexpr size_t off_OodleMalloc = 0x1576dc4;
constexpr size_t off_OodleFree = 0x1576dc8;
constexpr size_t off_OodleNetwork1_Shared_Size = 0x1171cc0;
constexpr size_t off_OodleNetwork1_Shared_SetWindow = 0x1171ba0;
constexpr size_t off_OodleNetwork1UDP_Train = 0x1170bb0;
constexpr size_t off_OodleNetwork1UDP_Decode = 0x11701a0;
constexpr size_t off_OodleNetwork1UDP_Encode = 0x11701e0;
constexpr size_t off_OodleNetwork1UDP_State_Size = 0x1170880;
#endif

void* __stdcall mymalloc(size_t size, int align) {
	const auto pRaw = (char*)malloc(size + align + sizeof(void*) - 1);
	if (!pRaw)
		return nullptr;

	const auto pAligned = (void*)(((size_t)pRaw + align + 7) & (size_t)-align);
	*((void**)pAligned - 1) = pRaw;
	return pAligned;
}

void __stdcall myfree(void* p) {
	free(*((void**)p - 1));
}

int main() {
	const auto virt = (uint8_t*)LoadLibraryW(GamePath);
	
	int htbits = 19;
	*(void**)&virt[off_OodleMalloc] = (void*)&mymalloc;
	*(void**)&virt[off_OodleFree] = (void*)&myfree;
	std::vector<uint8_t> state(((OodleNetwork1UDP_State_Size*)&virt[off_OodleNetwork1UDP_State_Size])());
	std::vector<uint8_t> shared(((OodleNetwork1_Shared_Size*)&virt[off_OodleNetwork1_Shared_Size])(htbits));
	std::vector<uint8_t> window(0x8000);

	((OodleNetwork1_Shared_SetWindow*)&virt[off_OodleNetwork1_Shared_SetWindow])(&shared[0], htbits, &window[0], static_cast<int>(window.size()));
	((OodleNetwork1UDP_Train*)&virt[off_OodleNetwork1UDP_Train])(&state[0], &shared[0], nullptr, nullptr, 0);

	std::vector<uint8_t> buf;
	std::vector<uint8_t> enc;
	std::vector<uint8_t> dec;

	while (true) {
		buf.resize(65536);
		enc.resize(buf.size());
		dec.resize(buf.size());

		for (auto& c : buf)
			c = rand();

		enc.resize(((OodleNetwork1UDP_Encode*)&virt[off_OodleNetwork1UDP_Encode])(&state[0], &shared[0], &buf[0], buf.size(), &enc[0]));

		((OodleNetwork1UDP_Decode*)&virt[off_OodleNetwork1UDP_Decode])(&state[0], &shared[0], &enc[0], enc.size(), &dec[0], dec.size());

		if (memcmp(&buf[0], &dec[0], buf.size()) != 0)
			__debugbreak();
	}
}