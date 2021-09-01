#include "pch.h"
#include <cryptopp/sha.h>

#include "XivAlex_SqexDef.h"

#include "Utils__Zlib.h"

const char XivAlex::SqexDef::SqpackHeader::Signature_Value[12] = {
	'S', 'q', 'P', 'a', 'c', 'k', 0, 0, 0, 0, 0, 0,
};

template<typename T, size_t C>
static bool IsAllSameValue(T(&arr)[C], std::remove_cv_t<T> supposedValue = 0) {
	for (size_t i = 0; i < C; ++i) {
		if (arr[i] != supposedValue)
			return false;
	}
	return true;
}

template<typename T>
static bool IsAllSameValue(const std::span<T>& arr, std::remove_cv_t<T> supposedValue = 0) {
	for (const auto& v : arr)
		if (v != supposedValue)
			return true;
	return true;
}

static void CalculateSha1(char(&result)[20], const void* data, size_t cb) {
	CryptoPP::SHA1 sha1;
	if (cb)
		sha1.Update(static_cast<const byte*>(data), cb);
	sha1.Final(reinterpret_cast<byte*>(result));
}

template<typename T>
static void CalculateSha1(char(&result)[20], const std::span<T>& data) {
	CalculateSha1(result, data.data(), data.size_bytes());
}

template<typename T>
static void VerifySha1(const char(&compareWith)[20], const std::span<T>& data, const char* throwMessage) {
	char result[20];
	CalculateSha1(result, data);
	if (memcmp(result, compareWith, 20) != 0) {
		if (data.empty() && IsAllSameValue(compareWith))
			return;
		throw XivAlex::SqexDef::InvalidSqpackException(throwMessage);
	}
}
const uint32_t XivAlex::SqexDef::SqexHashTable[4][256] = {
	{0x00000000, 0x77073096, 0xEE0E612C, 0x990951BA, 0x076DC419, 0x706AF48F, 0xE963A535, 0x9E6495A3, 0x0EDB8832,
	 0x79DCB8A4, 0xE0D5E91E, 0x97D2D988, 0x09B64C2B, 0x7EB17CBD, 0xE7B82D07, 0x90BF1D91, 0x1DB71064, 0x6AB020F2,
	 0xF3B97148, 0x84BE41DE, 0x1ADAD47D, 0x6DDDE4EB, 0xF4D4B551, 0x83D385C7, 0x136C9856, 0x646BA8C0, 0xFD62F97A,
	 0x8A65C9EC, 0x14015C4F, 0x63066CD9, 0xFA0F3D63, 0x8D080DF5, 0x3B6E20C8, 0x4C69105E, 0xD56041E4, 0xA2677172,
	 0x3C03E4D1, 0x4B04D447, 0xD20D85FD, 0xA50AB56B, 0x35B5A8FA, 0x42B2986C, 0xDBBBC9D6, 0xACBCF940, 0x32D86CE3,
	 0x45DF5C75, 0xDCD60DCF, 0xABD13D59, 0x26D930AC, 0x51DE003A, 0xC8D75180, 0xBFD06116, 0x21B4F4B5, 0x56B3C423,
	 0xCFBA9599, 0xB8BDA50F, 0x2802B89E, 0x5F058808, 0xC60CD9B2, 0xB10BE924, 0x2F6F7C87, 0x58684C11, 0xC1611DAB,
	 0xB6662D3D, 0x76DC4190, 0x01DB7106, 0x98D220BC, 0xEFD5102A, 0x71B18589, 0x06B6B51F, 0x9FBFE4A5, 0xE8B8D433,
	 0x7807C9A2, 0x0F00F934, 0x9609A88E, 0xE10E9818, 0x7F6A0DBB, 0x086D3D2D, 0x91646C97, 0xE6635C01, 0x6B6B51F4,
	 0x1C6C6162, 0x856530D8, 0xF262004E, 0x6C0695ED, 0x1B01A57B, 0x8208F4C1, 0xF50FC457, 0x65B0D9C6, 0x12B7E950,
	 0x8BBEB8EA, 0xFCB9887C, 0x62DD1DDF, 0x15DA2D49, 0x8CD37CF3, 0xFBD44C65, 0x4DB26158, 0x3AB551CE, 0xA3BC0074,
	 0xD4BB30E2, 0x4ADFA541, 0x3DD895D7, 0xA4D1C46D, 0xD3D6F4FB, 0x4369E96A, 0x346ED9FC, 0xAD678846, 0xDA60B8D0,
	 0x44042D73, 0x33031DE5, 0xAA0A4C5F, 0xDD0D7CC9, 0x5005713C, 0x270241AA, 0xBE0B1010, 0xC90C2086, 0x5768B525,
	 0x206F85B3, 0xB966D409, 0xCE61E49F, 0x5EDEF90E, 0x29D9C998, 0xB0D09822, 0xC7D7A8B4, 0x59B33D17, 0x2EB40D81,
	 0xB7BD5C3B, 0xC0BA6CAD, 0xEDB88320, 0x9ABFB3B6, 0x03B6E20C, 0x74B1D29A, 0xEAD54739, 0x9DD277AF, 0x04DB2615,
	 0x73DC1683, 0xE3630B12, 0x94643B84, 0x0D6D6A3E, 0x7A6A5AA8, 0xE40ECF0B, 0x9309FF9D, 0x0A00AE27, 0x7D079EB1,
	 0xF00F9344, 0x8708A3D2, 0x1E01F268, 0x6906C2FE, 0xF762575D, 0x806567CB, 0x196C3671, 0x6E6B06E7, 0xFED41B76,
	 0x89D32BE0, 0x10DA7A5A, 0x67DD4ACC, 0xF9B9DF6F, 0x8EBEEFF9, 0x17B7BE43, 0x60B08ED5, 0xD6D6A3E8, 0xA1D1937E,
	 0x38D8C2C4, 0x4FDFF252, 0xD1BB67F1, 0xA6BC5767, 0x3FB506DD, 0x48B2364B, 0xD80D2BDA, 0xAF0A1B4C, 0x36034AF6,
	 0x41047A60, 0xDF60EFC3, 0xA867DF55, 0x316E8EEF, 0x4669BE79, 0xCB61B38C, 0xBC66831A, 0x256FD2A0, 0x5268E236,
	 0xCC0C7795, 0xBB0B4703, 0x220216B9, 0x5505262F, 0xC5BA3BBE, 0xB2BD0B28, 0x2BB45A92, 0x5CB36A04, 0xC2D7FFA7,
	 0xB5D0CF31, 0x2CD99E8B, 0x5BDEAE1D, 0x9B64C2B0, 0xEC63F226, 0x756AA39C, 0x026D930A, 0x9C0906A9, 0xEB0E363F,
	 0x72076785, 0x05005713, 0x95BF4A82, 0xE2B87A14, 0x7BB12BAE, 0x0CB61B38, 0x92D28E9B, 0xE5D5BE0D, 0x7CDCEFB7,
	 0x0BDBDF21, 0x86D3D2D4, 0xF1D4E242, 0x68DDB3F8, 0x1FDA836E, 0x81BE16CD, 0xF6B9265B, 0x6FB077E1, 0x18B74777,
	 0x88085AE6, 0xFF0F6A70, 0x66063BCA, 0x11010B5C, 0x8F659EFF, 0xF862AE69, 0x616BFFD3, 0x166CCF45, 0xA00AE278,
	 0xD70DD2EE, 0x4E048354, 0x3903B3C2, 0xA7672661, 0xD06016F7, 0x4969474D, 0x3E6E77DB, 0xAED16A4A, 0xD9D65ADC,
	 0x40DF0B66, 0x37D83BF0, 0xA9BCAE53, 0xDEBB9EC5, 0x47B2CF7F, 0x30B5FFE9, 0xBDBDF21C, 0xCABAC28A, 0x53B39330,
	 0x24B4A3A6, 0xBAD03605, 0xCDD70693, 0x54DE5729, 0x23D967BF, 0xB3667A2E, 0xC4614AB8, 0x5D681B02, 0x2A6F2B94,
	 0xB40BBE37, 0xC30C8EA1, 0x5A05DF1B, 0x2D02EF8D},
	{0x00000000, 0x191B3141, 0x32366282, 0x2B2D53C3, 0x646CC504, 0x7D77F445, 0x565AA786, 0x4F4196C7, 0xC8D98A08,
	 0xD1C2BB49, 0xFAEFE88A, 0xE3F4D9CB, 0xACB54F0C, 0xB5AE7E4D, 0x9E832D8E, 0x87981CCF, 0x4AC21251, 0x53D92310,
	 0x78F470D3, 0x61EF4192, 0x2EAED755, 0x37B5E614, 0x1C98B5D7, 0x05838496, 0x821B9859, 0x9B00A918, 0xB02DFADB,
	 0xA936CB9A, 0xE6775D5D, 0xFF6C6C1C, 0xD4413FDF, 0xCD5A0E9E, 0x958424A2, 0x8C9F15E3, 0xA7B24620, 0xBEA97761,
	 0xF1E8E1A6, 0xE8F3D0E7, 0xC3DE8324, 0xDAC5B265, 0x5D5DAEAA, 0x44469FEB, 0x6F6BCC28, 0x7670FD69, 0x39316BAE,
	 0x202A5AEF, 0x0B07092C, 0x121C386D, 0xDF4636F3, 0xC65D07B2, 0xED705471, 0xF46B6530, 0xBB2AF3F7, 0xA231C2B6,
	 0x891C9175, 0x9007A034, 0x179FBCFB, 0x0E848DBA, 0x25A9DE79, 0x3CB2EF38, 0x73F379FF, 0x6AE848BE, 0x41C51B7D,
	 0x58DE2A3C, 0xF0794F05, 0xE9627E44, 0xC24F2D87, 0xDB541CC6, 0x94158A01, 0x8D0EBB40, 0xA623E883, 0xBF38D9C2,
	 0x38A0C50D, 0x21BBF44C, 0x0A96A78F, 0x138D96CE, 0x5CCC0009, 0x45D73148, 0x6EFA628B, 0x77E153CA, 0xBABB5D54,
	 0xA3A06C15, 0x888D3FD6, 0x91960E97, 0xDED79850, 0xC7CCA911, 0xECE1FAD2, 0xF5FACB93, 0x7262D75C, 0x6B79E61D,
	 0x4054B5DE, 0x594F849F, 0x160E1258, 0x0F152319, 0x243870DA, 0x3D23419B, 0x65FD6BA7, 0x7CE65AE6, 0x57CB0925,
	 0x4ED03864, 0x0191AEA3, 0x188A9FE2, 0x33A7CC21, 0x2ABCFD60, 0xAD24E1AF, 0xB43FD0EE, 0x9F12832D, 0x8609B26C,
	 0xC94824AB, 0xD05315EA, 0xFB7E4629, 0xE2657768, 0x2F3F79F6, 0x362448B7, 0x1D091B74, 0x04122A35, 0x4B53BCF2,
	 0x52488DB3, 0x7965DE70, 0x607EEF31, 0xE7E6F3FE, 0xFEFDC2BF, 0xD5D0917C, 0xCCCBA03D, 0x838A36FA, 0x9A9107BB,
	 0xB1BC5478, 0xA8A76539, 0x3B83984B, 0x2298A90A, 0x09B5FAC9, 0x10AECB88, 0x5FEF5D4F, 0x46F46C0E, 0x6DD93FCD,
	 0x74C20E8C, 0xF35A1243, 0xEA412302, 0xC16C70C1, 0xD8774180, 0x9736D747, 0x8E2DE606, 0xA500B5C5, 0xBC1B8484,
	 0x71418A1A, 0x685ABB5B, 0x4377E898, 0x5A6CD9D9, 0x152D4F1E, 0x0C367E5F, 0x271B2D9C, 0x3E001CDD, 0xB9980012,
	 0xA0833153, 0x8BAE6290, 0x92B553D1, 0xDDF4C516, 0xC4EFF457, 0xEFC2A794, 0xF6D996D5, 0xAE07BCE9, 0xB71C8DA8,
	 0x9C31DE6B, 0x852AEF2A, 0xCA6B79ED, 0xD37048AC, 0xF85D1B6F, 0xE1462A2E, 0x66DE36E1, 0x7FC507A0, 0x54E85463,
	 0x4DF36522, 0x02B2F3E5, 0x1BA9C2A4, 0x30849167, 0x299FA026, 0xE4C5AEB8, 0xFDDE9FF9, 0xD6F3CC3A, 0xCFE8FD7B,
	 0x80A96BBC, 0x99B25AFD, 0xB29F093E, 0xAB84387F, 0x2C1C24B0, 0x350715F1, 0x1E2A4632, 0x07317773, 0x4870E1B4,
	 0x516BD0F5, 0x7A468336, 0x635DB277, 0xCBFAD74E, 0xD2E1E60F, 0xF9CCB5CC, 0xE0D7848D, 0xAF96124A, 0xB68D230B,
	 0x9DA070C8, 0x84BB4189, 0x03235D46, 0x1A386C07, 0x31153FC4, 0x280E0E85, 0x674F9842, 0x7E54A903, 0x5579FAC0,
	 0x4C62CB81, 0x8138C51F, 0x9823F45E, 0xB30EA79D, 0xAA1596DC, 0xE554001B, 0xFC4F315A, 0xD7626299, 0xCE7953D8,
	 0x49E14F17, 0x50FA7E56, 0x7BD72D95, 0x62CC1CD4, 0x2D8D8A13, 0x3496BB52, 0x1FBBE891, 0x06A0D9D0, 0x5E7EF3EC,
	 0x4765C2AD, 0x6C48916E, 0x7553A02F, 0x3A1236E8, 0x230907A9, 0x0824546A, 0x113F652B, 0x96A779E4, 0x8FBC48A5,
	 0xA4911B66, 0xBD8A2A27, 0xF2CBBCE0, 0xEBD08DA1, 0xC0FDDE62, 0xD9E6EF23, 0x14BCE1BD, 0x0DA7D0FC, 0x268A833F,
	 0x3F91B27E, 0x70D024B9, 0x69CB15F8, 0x42E6463B, 0x5BFD777A, 0xDC656BB5, 0xC57E5AF4, 0xEE530937, 0xF7483876,
	 0xB809AEB1, 0xA1129FF0, 0x8A3FCC33, 0x9324FD72},
	{0x00000000, 0x01C26A37, 0x0384D46E, 0x0246BE59, 0x0709A8DC, 0x06CBC2EB, 0x048D7CB2, 0x054F1685, 0x0E1351B8,
	 0x0FD13B8F, 0x0D9785D6, 0x0C55EFE1, 0x091AF964, 0x08D89353, 0x0A9E2D0A, 0x0B5C473D, 0x1C26A370, 0x1DE4C947,
	 0x1FA2771E, 0x1E601D29, 0x1B2F0BAC, 0x1AED619B, 0x18ABDFC2, 0x1969B5F5, 0x1235F2C8, 0x13F798FF, 0x11B126A6,
	 0x10734C91, 0x153C5A14, 0x14FE3023, 0x16B88E7A, 0x177AE44D, 0x384D46E0, 0x398F2CD7, 0x3BC9928E, 0x3A0BF8B9,
	 0x3F44EE3C, 0x3E86840B, 0x3CC03A52, 0x3D025065, 0x365E1758, 0x379C7D6F, 0x35DAC336, 0x3418A901, 0x3157BF84,
	 0x3095D5B3, 0x32D36BEA, 0x331101DD, 0x246BE590, 0x25A98FA7, 0x27EF31FE, 0x262D5BC9, 0x23624D4C, 0x22A0277B,
	 0x20E69922, 0x2124F315, 0x2A78B428, 0x2BBADE1F, 0x29FC6046, 0x283E0A71, 0x2D711CF4, 0x2CB376C3, 0x2EF5C89A,
	 0x2F37A2AD, 0x709A8DC0, 0x7158E7F7, 0x731E59AE, 0x72DC3399, 0x7793251C, 0x76514F2B, 0x7417F172, 0x75D59B45,
	 0x7E89DC78, 0x7F4BB64F, 0x7D0D0816, 0x7CCF6221, 0x798074A4, 0x78421E93, 0x7A04A0CA, 0x7BC6CAFD, 0x6CBC2EB0,
	 0x6D7E4487, 0x6F38FADE, 0x6EFA90E9, 0x6BB5866C, 0x6A77EC5B, 0x68315202, 0x69F33835, 0x62AF7F08, 0x636D153F,
	 0x612BAB66, 0x60E9C151, 0x65A6D7D4, 0x6464BDE3, 0x662203BA, 0x67E0698D, 0x48D7CB20, 0x4915A117, 0x4B531F4E,
	 0x4A917579, 0x4FDE63FC, 0x4E1C09CB, 0x4C5AB792, 0x4D98DDA5, 0x46C49A98, 0x4706F0AF, 0x45404EF6, 0x448224C1,
	 0x41CD3244, 0x400F5873, 0x4249E62A, 0x438B8C1D, 0x54F16850, 0x55330267, 0x5775BC3E, 0x56B7D609, 0x53F8C08C,
	 0x523AAABB, 0x507C14E2, 0x51BE7ED5, 0x5AE239E8, 0x5B2053DF, 0x5966ED86, 0x58A487B1, 0x5DEB9134, 0x5C29FB03,
	 0x5E6F455A, 0x5FAD2F6D, 0xE1351B80, 0xE0F771B7, 0xE2B1CFEE, 0xE373A5D9, 0xE63CB35C, 0xE7FED96B, 0xE5B86732,
	 0xE47A0D05, 0xEF264A38, 0xEEE4200F, 0xECA29E56, 0xED60F461, 0xE82FE2E4, 0xE9ED88D3, 0xEBAB368A, 0xEA695CBD,
	 0xFD13B8F0, 0xFCD1D2C7, 0xFE976C9E, 0xFF5506A9, 0xFA1A102C, 0xFBD87A1B, 0xF99EC442, 0xF85CAE75, 0xF300E948,
	 0xF2C2837F, 0xF0843D26, 0xF1465711, 0xF4094194, 0xF5CB2BA3, 0xF78D95FA, 0xF64FFFCD, 0xD9785D60, 0xD8BA3757,
	 0xDAFC890E, 0xDB3EE339, 0xDE71F5BC, 0xDFB39F8B, 0xDDF521D2, 0xDC374BE5, 0xD76B0CD8, 0xD6A966EF, 0xD4EFD8B6,
	 0xD52DB281, 0xD062A404, 0xD1A0CE33, 0xD3E6706A, 0xD2241A5D, 0xC55EFE10, 0xC49C9427, 0xC6DA2A7E, 0xC7184049,
	 0xC25756CC, 0xC3953CFB, 0xC1D382A2, 0xC011E895, 0xCB4DAFA8, 0xCA8FC59F, 0xC8C97BC6, 0xC90B11F1, 0xCC440774,
	 0xCD866D43, 0xCFC0D31A, 0xCE02B92D, 0x91AF9640, 0x906DFC77, 0x922B422E, 0x93E92819, 0x96A63E9C, 0x976454AB,
	 0x9522EAF2, 0x94E080C5, 0x9FBCC7F8, 0x9E7EADCF, 0x9C381396, 0x9DFA79A1, 0x98B56F24, 0x99770513, 0x9B31BB4A,
	 0x9AF3D17D, 0x8D893530, 0x8C4B5F07, 0x8E0DE15E, 0x8FCF8B69, 0x8A809DEC, 0x8B42F7DB, 0x89044982, 0x88C623B5,
	 0x839A6488, 0x82580EBF, 0x801EB0E6, 0x81DCDAD1, 0x8493CC54, 0x8551A663, 0x8717183A, 0x86D5720D, 0xA9E2D0A0,
	 0xA820BA97, 0xAA6604CE, 0xABA46EF9, 0xAEEB787C, 0xAF29124B, 0xAD6FAC12, 0xACADC625, 0xA7F18118, 0xA633EB2F,
	 0xA4755576, 0xA5B73F41, 0xA0F829C4, 0xA13A43F3, 0xA37CFDAA, 0xA2BE979D, 0xB5C473D0, 0xB40619E7, 0xB640A7BE,
	 0xB782CD89, 0xB2CDDB0C, 0xB30FB13B, 0xB1490F62, 0xB08B6555, 0xBBD72268, 0xBA15485F, 0xB853F606, 0xB9919C31,
	 0xBCDE8AB4, 0xBD1CE083, 0xBF5A5EDA, 0xBE9834ED},
	{0x00000000, 0xB8BC6765, 0xAA09C88B, 0x12B5AFEE, 0x8F629757, 0x37DEF032, 0x256B5FDC, 0x9DD738B9, 0xC5B428EF,
	 0x7D084F8A, 0x6FBDE064, 0xD7018701, 0x4AD6BFB8, 0xF26AD8DD, 0xE0DF7733, 0x58631056, 0x5019579F, 0xE8A530FA,
	 0xFA109F14, 0x42ACF871, 0xDF7BC0C8, 0x67C7A7AD, 0x75720843, 0xCDCE6F26, 0x95AD7F70, 0x2D111815, 0x3FA4B7FB,
	 0x8718D09E, 0x1ACFE827, 0xA2738F42, 0xB0C620AC, 0x087A47C9, 0xA032AF3E, 0x188EC85B, 0x0A3B67B5, 0xB28700D0,
	 0x2F503869, 0x97EC5F0C, 0x8559F0E2, 0x3DE59787, 0x658687D1, 0xDD3AE0B4, 0xCF8F4F5A, 0x7733283F, 0xEAE41086,
	 0x525877E3, 0x40EDD80D, 0xF851BF68, 0xF02BF8A1, 0x48979FC4, 0x5A22302A, 0xE29E574F, 0x7F496FF6, 0xC7F50893,
	 0xD540A77D, 0x6DFCC018, 0x359FD04E, 0x8D23B72B, 0x9F9618C5, 0x272A7FA0, 0xBAFD4719, 0x0241207C, 0x10F48F92,
	 0xA848E8F7, 0x9B14583D, 0x23A83F58, 0x311D90B6, 0x89A1F7D3, 0x1476CF6A, 0xACCAA80F, 0xBE7F07E1, 0x06C36084,
	 0x5EA070D2, 0xE61C17B7, 0xF4A9B859, 0x4C15DF3C, 0xD1C2E785, 0x697E80E0, 0x7BCB2F0E, 0xC377486B, 0xCB0D0FA2,
	 0x73B168C7, 0x6104C729, 0xD9B8A04C, 0x446F98F5, 0xFCD3FF90, 0xEE66507E, 0x56DA371B, 0x0EB9274D, 0xB6054028,
	 0xA4B0EFC6, 0x1C0C88A3, 0x81DBB01A, 0x3967D77F, 0x2BD27891, 0x936E1FF4, 0x3B26F703, 0x839A9066, 0x912F3F88,
	 0x299358ED, 0xB4446054, 0x0CF80731, 0x1E4DA8DF, 0xA6F1CFBA, 0xFE92DFEC, 0x462EB889, 0x549B1767, 0xEC277002,
	 0x71F048BB, 0xC94C2FDE, 0xDBF98030, 0x6345E755, 0x6B3FA09C, 0xD383C7F9, 0xC1366817, 0x798A0F72, 0xE45D37CB,
	 0x5CE150AE, 0x4E54FF40, 0xF6E89825, 0xAE8B8873, 0x1637EF16, 0x048240F8, 0xBC3E279D, 0x21E91F24, 0x99557841,
	 0x8BE0D7AF, 0x335CB0CA, 0xED59B63B, 0x55E5D15E, 0x47507EB0, 0xFFEC19D5, 0x623B216C, 0xDA874609, 0xC832E9E7,
	 0x708E8E82, 0x28ED9ED4, 0x9051F9B1, 0x82E4565F, 0x3A58313A, 0xA78F0983, 0x1F336EE6, 0x0D86C108, 0xB53AA66D,
	 0xBD40E1A4, 0x05FC86C1, 0x1749292F, 0xAFF54E4A, 0x322276F3, 0x8A9E1196, 0x982BBE78, 0x2097D91D, 0x78F4C94B,
	 0xC048AE2E, 0xD2FD01C0, 0x6A4166A5, 0xF7965E1C, 0x4F2A3979, 0x5D9F9697, 0xE523F1F2, 0x4D6B1905, 0xF5D77E60,
	 0xE762D18E, 0x5FDEB6EB, 0xC2098E52, 0x7AB5E937, 0x680046D9, 0xD0BC21BC, 0x88DF31EA, 0x3063568F, 0x22D6F961,
	 0x9A6A9E04, 0x07BDA6BD, 0xBF01C1D8, 0xADB46E36, 0x15080953, 0x1D724E9A, 0xA5CE29FF, 0xB77B8611, 0x0FC7E174,
	 0x9210D9CD, 0x2AACBEA8, 0x38191146, 0x80A57623, 0xD8C66675, 0x607A0110, 0x72CFAEFE, 0xCA73C99B, 0x57A4F122,
	 0xEF189647, 0xFDAD39A9, 0x45115ECC, 0x764DEE06, 0xCEF18963, 0xDC44268D, 0x64F841E8, 0xF92F7951, 0x41931E34,
	 0x5326B1DA, 0xEB9AD6BF, 0xB3F9C6E9, 0x0B45A18C, 0x19F00E62, 0xA14C6907, 0x3C9B51BE, 0x842736DB, 0x96929935,
	 0x2E2EFE50, 0x2654B999, 0x9EE8DEFC, 0x8C5D7112, 0x34E11677, 0xA9362ECE, 0x118A49AB, 0x033FE645, 0xBB838120,
	 0xE3E09176, 0x5B5CF613, 0x49E959FD, 0xF1553E98, 0x6C820621, 0xD43E6144, 0xC68BCEAA, 0x7E37A9CF, 0xD67F4138,
	 0x6EC3265D, 0x7C7689B3, 0xC4CAEED6, 0x591DD66F, 0xE1A1B10A, 0xF3141EE4, 0x4BA87981, 0x13CB69D7, 0xAB770EB2,
	 0xB9C2A15C, 0x017EC639, 0x9CA9FE80, 0x241599E5, 0x36A0360B, 0x8E1C516E, 0x866616A7, 0x3EDA71C2, 0x2C6FDE2C,
	 0x94D3B949, 0x090481F0, 0xB1B8E695, 0xA30D497B, 0x1BB12E1E, 0x43D23E48, 0xFB6E592D, 0xE9DBF6C3, 0x516791A6,
	 0xCCB0A91F, 0x740CCE7A, 0x66B96194, 0xDE0506F1},
};

void XivAlex::SqexDef::SqpackHeader::VerifySqpackHeader(SqpackType supposedType) {
	if (HeaderSize != sizeof SqpackHeader)
		throw InvalidSqpackException("sizeof Header != 0x400");
	if (memcmp(Signature, Signature_Value, sizeof Signature) != 0)
		throw InvalidSqpackException("Invalid SqPack signature");
	VerifySha1(Sha1, std::span(reinterpret_cast<const char*>(this), offsetof(SqexDef::SqpackHeader, Sha1)), "Header SHA-1");
	if (!IsAllSameValue(Padding_0x024))
		throw InvalidSqpackException("Padding_0x024 != 0");
	if (!IsAllSameValue(Padding_0x3D4))
		throw InvalidSqpackException("Padding_0x3D4 != 0");
	if (supposedType != SqpackType::Unspecified && supposedType != Type)
		throw InvalidSqpackException(std::format("Invalid SqpackType (expected {}, file is {})",
			static_cast<uint32_t>(supposedType),
			static_cast<uint32_t>(Type.Value())));
}


void XivAlex::SqexDef::SqIndex::Header::VerifySqpackIndexHeader(IndexType expectedIndexType) {
	if (HeaderSize != sizeof Header)
		throw InvalidSqpackException("sizeof IndexHeader != 0x400");
	if (expectedIndexType != IndexType::Unspecified && expectedIndexType != Type)
		throw InvalidSqpackException(std::format("Invalid SqpackType (expected {}, file is {})",
			static_cast<uint32_t>(expectedIndexType),
			static_cast<uint32_t>(Type.Value())));
	VerifySha1(Sha1, std::span(reinterpret_cast<const char*>(this), offsetof(SqexDef::SqIndex::Header, Sha1)), "IndexHeader SHA-1");
	if (!IsAllSameValue(Padding_0x04C))
		throw InvalidSqpackException("Padding_0x04C");
	if (!IsAllSameValue(Padding_0x128))
		throw InvalidSqpackException("Padding_0x128");
	if (!IsAllSameValue(Padding_0x130))
		throw InvalidSqpackException("Padding_0x130");
	if (!IsAllSameValue(Padding_0x3D4))
		throw InvalidSqpackException("Padding_0x3D4");

	if (!IsAllSameValue(FileSegment.Padding_0x020))
		throw InvalidSqpackException("FileSegment.Padding_0x020");
	if (!IsAllSameValue(DataFilesSegment.Padding_0x020))
		throw InvalidSqpackException("DataFilesSegment.Padding_0x020");
	if (!IsAllSameValue(UnknownSegment3.Padding_0x020))
		throw InvalidSqpackException("UnknownSegment3.Padding_0x020");
	if (!IsAllSameValue(FolderSegment.Padding_0x020))
		throw InvalidSqpackException("FolderSegment.Padding_0x020");

	if (Type == IndexType::Index && FileSegment.Size % sizeof FileSegmentEntry)
		throw InvalidSqpackException("FileSegment.size % sizeof FileSegmentEntry != 0");
	else if (Type == IndexType::Index2 && FileSegment.Size % sizeof FileSegmentEntry2)
		throw InvalidSqpackException("FileSegment.size % sizeof FileSegmentEntry2 != 0");
	if (UnknownSegment3.Size % sizeof Segment3Entry)
		throw InvalidSqpackException("UnknownSegment3.size % sizeof Segment3Entry != 0");
	if (FolderSegment.Size % sizeof FolderSegmentEntry)
		throw InvalidSqpackException("FolderSegment.size % sizeof FolderSegmentEntry != 0");

	if (FileSegment.Count != 1)
		throw InvalidSqpackException("Segment1.Count == 1");
	if (UnknownSegment3.Count != 0)
		throw InvalidSqpackException("Segment3.Count == 0");
	if (FolderSegment.Count != 0)
		throw InvalidSqpackException("Segment4.Count == 0");
}

void XivAlex::SqexDef::SqIndex::Header::VerifyDataFileSegment(const std::vector<char>& DataFileSegment) {
	if (DataFilesSegment.Size == 0x100) {
		if (!IsAllSameValue(std::span(&DataFileSegment[16], DataFileSegment.size() - 16), '\xff'))
			throw InvalidSqpackException("DataFileSegment.0-7 != 0xFF");
		if (!IsAllSameValue(std::span(&DataFileSegment[16], DataFileSegment.size() - 16)))
			throw InvalidSqpackException("DataFileSegment.8-11 != 0");
		if (!IsAllSameValue(std::span(&DataFileSegment[16], DataFileSegment.size() - 16), '\xff'))
			throw InvalidSqpackException("DataFileSegment.12-15 != 0xFF");
		if (!IsAllSameValue(std::span(&DataFileSegment[16], DataFileSegment.size() - 16)))
			throw InvalidSqpackException("DataFileSegment.16+ != 0x00");
	}
}

XivAlex::SqexDef::SqIndex::LEDataLocator::LEDataLocator(uint32_t index, uint64_t offset)
	: LE<uint32_t>(0) {
	Index(index);
	Offset(offset);
}

uint32_t XivAlex::SqexDef::SqIndex::LEDataLocator::Index() const {
	return (Value() & 0xF) / 2;
}

uint64_t XivAlex::SqexDef::SqIndex::LEDataLocator::Offset() const {
	return (Value() & 0xFFFFFFF0UL) * 8ULL;
}

uint32_t XivAlex::SqexDef::SqIndex::LEDataLocator::Index(const uint32_t value) {
	if (value >= 8)
		throw std::invalid_argument("Index must be between 0 and 7.");
	Value((Value() & ~0x0F) | (value * 2));
	return value;
}

uint64_t XivAlex::SqexDef::SqIndex::LEDataLocator::Offset(const uint64_t value) {
	if (value % 128)
		throw std::invalid_argument("OffsetAfterHeaders must be a multiple of 128.");
	const auto divValue = value / 8;
	if (divValue > UINT32_MAX)
		throw std::invalid_argument("Value too big.");
	Value(static_cast<uint32_t>((Value() & 0x0F) | divValue));
	return value;
}

void XivAlex::SqexDef::SqIndex::FolderSegmentEntry::Verify() const {
	if (FileSegmentSize % sizeof(FileSegmentEntry))
		throw InvalidSqpackException("FolderSegmentEntry.FileSegmentSize % sizeof FileSegmentEntry != 0");
}

void XivAlex::SqexDef::SqData::Header::Verify(uint32_t expectedSpanIndex) const {
	if (HeaderSize != sizeof Header)
		throw InvalidSqpackException("sizeof IndexHeader != 0x400");
	VerifySha1(Sha1, std::span(reinterpret_cast<const char*>(this), offsetof(SqexDef::SqData::Header, Sha1)), "IndexHeader SHA-1");
	if (Null1.Value())
		throw InvalidSqpackException("Null1 != 0");
	if (Unknown1 != Unknown1_Value)
		throw InvalidSqpackException(std::format("Unknown1({:x}) != Unknown1_Value({:x})", Unknown1.Value(), Unknown1_Value));
	if (SpanIndex != expectedSpanIndex)
		throw InvalidSqpackException(std::format("SpanIndex({}) != ExpectedSpanIndex({})", SpanIndex.Value(), expectedSpanIndex));
	if (Null2.Value())
		throw InvalidSqpackException("Null2 != 0");
	if (MaxFileSize > MaxFileSize_MaxValue)
		throw InvalidSqpackException(std::format("MaxFileSize({:x}) != MaxFileSize_MaxValue({:x})", MaxFileSize.Value(), MaxFileSize_MaxValue));
	if (!IsAllSameValue(Padding_0x034))
		throw InvalidSqpackException("Padding_0x034 != 0");
	if (!IsAllSameValue(Padding_0x3D4))
		throw InvalidSqpackException("Padding_0x3D4 != 0");
}

uint32_t XivAlex::SqexDef::SqexHash(const char* data, size_t len) {
	std::string normalizedText(data, len);
	for (auto& c : normalizedText) {
		if ('A' <= c && c <= 'Z')
			c -= 'A' - 'a';
		else if (c == '\\')
			c = '/';
	}
	size_t i = 0;
	uint32_t result = 0xFFFFFFFFUL;
	for (; i < (len & ~3); i += 4) {
		result ^= *reinterpret_cast<const uint32_t*>(&normalizedText[i]);
		result = SqexHashTable[3][result & 0xFF] ^
			SqexHashTable[2][(result >> 8) & 0xFF] ^
			SqexHashTable[1][(result >> 16) & 0xFF] ^
			SqexHashTable[0][(result >> 24) & 0xFF];
	}

	for (; i < len; ++i)
		result = SqexHashTable[0][(result ^ normalizedText[i]) & 0xFF] ^ (result >> 8);

	return result;
}

uint32_t XivAlex::SqexDef::SqexHash(const std::string& text) {
	return SqexHash(text.data(), text.size());
}

uint32_t XivAlex::SqexDef::SqexHash(const std::string_view& text) {
	return SqexHash(text.data(), text.size());
}

XivAlex::SqexDef::FileSystemSqPack::SqIndex::SqIndex(const Utils::Win32::File& hFile, bool strictVerify) {
	std::vector<std::pair<size_t, size_t>> accesses;

	hFile.Read(0, &Header, sizeof SqpackHeader);
	if (strictVerify) {
		accesses.emplace_back(0, Header.HeaderSize);
		Header.VerifySqpackHeader(SqpackType::SqIndex);
	}

	hFile.Read(Header.HeaderSize, &IndexHeader, sizeof SqexDef::SqIndex::Header);
	if (strictVerify) {
		accesses.emplace_back(Header.HeaderSize, IndexHeader.HeaderSize);
		IndexHeader.VerifySqpackIndexHeader(SqexDef::SqIndex::Header::IndexType::Index);
	}

	DataFileSegment.resize(IndexHeader.DataFilesSegment.Size);
	hFile.Read(IndexHeader.DataFilesSegment.Offset, std::span(DataFileSegment));
	if (strictVerify) {
		accesses.emplace_back(IndexHeader.DataFilesSegment.Offset, IndexHeader.DataFilesSegment.Size);
		VerifySha1(IndexHeader.DataFilesSegment.Sha1, std::span(DataFileSegment), "DataFilesSegment Data SHA-1");
		IndexHeader.VerifyDataFileSegment(DataFileSegment);
	}

	Segment3.resize(IndexHeader.UnknownSegment3.Size / sizeof(SqexDef::SqIndex::Segment3Entry));
	hFile.Read(IndexHeader.UnknownSegment3.Offset, std::span(Segment3));
	if (strictVerify) {
		accesses.emplace_back(IndexHeader.UnknownSegment3.Offset, IndexHeader.UnknownSegment3.Size);
		VerifySha1(IndexHeader.UnknownSegment3.Sha1, std::span(Segment3), "UnknownSegment3 Data SHA-1");
	}

	Folders.resize(IndexHeader.FolderSegment.Size / sizeof(SqexDef::SqIndex::FolderSegmentEntry));
	hFile.Read(IndexHeader.FolderSegment.Offset, std::span(Folders));
	if (strictVerify) {
		accesses.emplace_back(IndexHeader.FolderSegment.Offset, IndexHeader.FolderSegment.Size);
		VerifySha1(IndexHeader.FolderSegment.Sha1, std::span(Folders), "FolderSegment Data SHA-1");
	}

	{
		uint32_t lastEnd = IndexHeader.FileSegment.Offset;
		for (const auto& folder : Folders) {
			if (strictVerify)
				folder.Verify();

			auto& filesInFolder = Files[folder.NameHash];
			filesInFolder.resize(folder.FileSegmentSize / sizeof(SqexDef::SqIndex::FileSegmentEntry));
			hFile.Read(folder.FileSegmentOffset, std::span(filesInFolder));

			if (folder.FileSegmentOffset >= IndexHeader.FileSegment.Offset &&
				folder.FileSegmentOffset < IndexHeader.FileSegment.Offset + IndexHeader.FileSegment.Size) {
				if (strictVerify) {
					accesses.emplace_back(folder.FileSegmentOffset, folder.FileSegmentSize);
					if (lastEnd != folder.FileSegmentOffset)
						throw InvalidSqpackException("last directory listing end != new directory listing start");
					for (const auto& file : filesInFolder)
						if (file.PathHash != folder.NameHash)
							throw InvalidSqpackException("file path hash != folder name hash");
				}
				lastEnd += folder.FileSegmentSize;
			} else if (folder.FileSegmentOffset >= IndexHeader.DataFilesSegment.Offset &&
				folder.FileSegmentOffset < IndexHeader.DataFilesSegment.Offset + IndexHeader.DataFilesSegment.Size) {
				Files.erase(folder.NameHash);
				// ignore for now
			} else if (folder.FileSegmentOffset >= IndexHeader.UnknownSegment3.Offset &&
				folder.FileSegmentOffset < IndexHeader.UnknownSegment3.Offset + IndexHeader.UnknownSegment3.Size) {
				Files.erase(folder.NameHash);
				// ignore for now
			}
		}

		if (strictVerify) {
			if (lastEnd != IndexHeader.FileSegment.Offset + IndexHeader.FileSegment.Size)
				throw InvalidSqpackException("last directory listing end != end of file segment");

			char result[20];
			CryptoPP::SHA1 sha1;
			for (const auto& [_, files] : Files)
				sha1.Update(reinterpret_cast<const byte*>(&files[0]), std::span(files).size_bytes());
			sha1.Final(reinterpret_cast<byte*>(result));
			if (memcmp(result, IndexHeader.FileSegment.Sha1, 20) != 0) {
				if (IndexHeader.FileSegment.Size == 0 && IsAllSameValue(IndexHeader.FileSegment.Sha1)) {
					// pass
				} else
					throw InvalidSqpackException("FileSegment Data SHA-1");
			}
		}
	}

	if (strictVerify) {
		std::sort(accesses.begin(), accesses.end());
		size_t ptr = accesses[0].first;
		for (const auto& [accessPointer, accessSize] : accesses) {
			if (ptr > accessPointer)
				throw InvalidSqpackException("Unread region found");
			else if (ptr < accessPointer)
				throw InvalidSqpackException("Overlapping region found");
			ptr += accessSize;
		}
		if (ptr != hFile.Length())
			throw InvalidSqpackException("Trailing region found");
	}
}

XivAlex::SqexDef::FileSystemSqPack::SqIndex2::SqIndex2(const Utils::Win32::File& hFile, bool strictVerify) {
	std::vector<std::pair<size_t, size_t>> accesses;

	hFile.Read(0, &Header, sizeof SqpackHeader);
	if (strictVerify) {
		accesses.emplace_back(0, Header.HeaderSize);
		Header.VerifySqpackHeader(SqpackType::SqIndex);
	}

	hFile.Read(Header.HeaderSize, &IndexHeader, sizeof SqexDef::SqIndex::Header);
	accesses.emplace_back(Header.HeaderSize, IndexHeader.HeaderSize);
	if (strictVerify)
		IndexHeader.VerifySqpackIndexHeader(SqexDef::SqIndex::Header::IndexType::Index2);

	DataFileSegment.resize(IndexHeader.DataFilesSegment.Size);
	hFile.Read(IndexHeader.DataFilesSegment.Offset, std::span(DataFileSegment));
	if (strictVerify) {
		accesses.emplace_back(IndexHeader.DataFilesSegment.Offset, IndexHeader.DataFilesSegment.Size);
		VerifySha1(IndexHeader.DataFilesSegment.Sha1, std::span(DataFileSegment), "DataFilesSegment Data SHA-1");
		IndexHeader.VerifyDataFileSegment(DataFileSegment);
	}

	Segment3.resize(IndexHeader.UnknownSegment3.Size / sizeof(SqexDef::SqIndex::Segment3Entry));
	hFile.Read(IndexHeader.UnknownSegment3.Offset, std::span(Segment3));
	if (strictVerify) {
		accesses.emplace_back(IndexHeader.UnknownSegment3.Offset, IndexHeader.UnknownSegment3.Size);
		VerifySha1(IndexHeader.UnknownSegment3.Sha1, std::span(Segment3), "UnknownSegment3 Data SHA-1");
	}

	Folders.resize(IndexHeader.FolderSegment.Size / sizeof(SqexDef::SqIndex::FolderSegmentEntry));
	hFile.Read(IndexHeader.FolderSegment.Offset, std::span(Folders));
	if (strictVerify) {
		accesses.emplace_back(IndexHeader.FolderSegment.Offset, IndexHeader.FolderSegment.Size);
		VerifySha1(IndexHeader.FolderSegment.Sha1, std::span(Folders), "FolderSegment Data SHA-1");
	}

	Files.resize(IndexHeader.FileSegment.Size / sizeof(SqexDef::SqIndex::FileSegmentEntry2));
	hFile.Read(IndexHeader.FileSegment.Offset, std::span(Files));
	if (strictVerify) {
		accesses.emplace_back(IndexHeader.FileSegment.Offset, IndexHeader.FileSegment.Size);
		VerifySha1(IndexHeader.FileSegment.Sha1, std::span(Files), "FolderSegment Data SHA-1");
	}

	if (strictVerify) {
		std::sort(accesses.begin(), accesses.end());
		size_t ptr = accesses[0].first;
		for (const auto& [accessPointer, accessSize] : accesses) {
			if (ptr > accessPointer)
				throw InvalidSqpackException("Unread region found");
			else if (ptr < accessPointer)
				throw InvalidSqpackException("Overlapping region found");
			ptr += accessSize;
		}
		if (ptr != hFile.Length())
			throw InvalidSqpackException("Trailing region found");
	}
}

XivAlex::SqexDef::FileSystemSqPack::SqData::SqData(Utils::Win32::File hFile, const uint32_t datIndex, std::vector<SqDataEntry>& dataEntries, bool strictVerify)
	: FileOnDisk(std::move(hFile)) {
	std::vector<std::pair<size_t, size_t>> accesses;

	FileOnDisk.Read(0, &Header, sizeof SqpackHeader);
	if (strictVerify) {
		if (datIndex == 0)
			Header.VerifySqpackHeader(SqpackType::SqData);
		accesses.emplace_back(0, sizeof SqpackHeader);
	}

	FileOnDisk.Read(sizeof SqpackHeader, &DataHeader, sizeof SqexDef::SqData::Header);
	if (strictVerify) {
		DataHeader.Verify(datIndex + 1);
		accesses.emplace_back(sizeof SqpackHeader, sizeof SqexDef::SqData::Header);
	}

	const auto dataFileLength = FileOnDisk.Length();
	if (strictVerify) {
		if (dataFileLength != 0ULL + Header.HeaderSize + DataHeader.HeaderSize + DataHeader.DataSize)
			throw InvalidSqpackException("Invalid file size");
	}

	std::map<uint64_t, SqDataEntry*> offsetToEntryMap;
	for (auto& file : dataEntries) {
		if (file.IndexEntry.DatFile.Index() != datIndex)
			continue;
		offsetToEntryMap.insert_or_assign(file.IndexEntry.DatFile.Offset(), &file);
	}

	SqDataEntry* prevEntry = nullptr;
	for (const auto& [begin, entry] : offsetToEntryMap) {
		if (prevEntry)
			prevEntry->DataEntrySize = static_cast<uint32_t>(begin - prevEntry->IndexEntry.DatFile.Offset());
		prevEntry = entry;
	}
	if (prevEntry)
		prevEntry->DataEntrySize = static_cast<uint32_t>(dataFileLength - prevEntry->IndexEntry.DatFile.Offset());
}

XivAlex::SqexDef::FileSystemSqPack::FileSystemSqPack(const std::filesystem::path& indexFile, bool strictVerify)
	: Index(Utils::Win32::File::Create(std::filesystem::path(indexFile).replace_extension(".index"), GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING, 0), strictVerify)
	, Index2(Utils::Win32::File::Create(std::filesystem::path(indexFile).replace_extension(".index2"), GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING, 0), strictVerify) {

	std::map<uint64_t, SqDataEntry*> offsetToEntryMap;

	Files.reserve(Index2.Files.size());
	for (const auto& entry : Index2.Files) {
		Files.emplace_back(SqDataEntry{
			.Index2Entry = entry,
			.DataEntryOffset = entry.DatFile.Offset(),
			.DataFileIndex = entry.DatFile.Index(),
			});
		offsetToEntryMap[entry.DatFile] = &Files.back();
	}

	std::vector<SqexDef::SqIndex::FileSegmentEntry*> newEntries;
	for (auto& [path, files] : Index.Files) {
		for (auto& entry : files) {
			const auto ptr = offsetToEntryMap[entry.DatFile];
			if (!ptr)
				newEntries.push_back(&entry);
			else
				ptr->IndexEntry = entry;
		}
	}
	for (const auto entry : newEntries)
		Files.emplace_back(SqDataEntry{
			.IndexEntry = *entry,
			.DataEntryOffset = entry->DatFile.Offset(),
			.DataFileIndex = entry->DatFile.Index(),
			});

	Data.reserve(Index.IndexHeader.DataFilesSegment.Count);
	for (uint32_t i = 0; i < Index.IndexHeader.DataFilesSegment.Count; ++i) {
		Data.emplace_back(SqData{
			Utils::Win32::File::Create(std::filesystem::path(indexFile).replace_extension(std::format(".dat{}", i)), GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING, 0),
			i,
			Files,
			strictVerify,
			});
	}
}

uint32_t XivAlex::SqexDef::VirtualSqPack::EmptyEntryProvider::Size() const {
	return sizeof SqData::FileEntryHeader;
}

size_t XivAlex::SqexDef::VirtualSqPack::EmptyEntryProvider::Read(const uint64_t offset, void* const buf, const size_t length) const {
	if (offset < sizeof SqData::FileEntryHeader) {
		const auto header = SqData::FileEntryHeader{
			.HeaderSize = sizeof SqData::FileEntryHeader,
			.Type = SqData::FileEntryType::Binary,
			.DecompressedSize = 0,
			.Unknown1 = 0,
			.BlockBufferSize = 0,
			.BlockCount = 0,
		};
		const auto out = std::span(static_cast<char*>(buf), length);
		const auto src = std::span(reinterpret_cast<const char*>(&header), header.HeaderSize)
			.subspan(static_cast<size_t>(offset));
		const auto available = std::min(out.size_bytes(), src.size_bytes());
		std::copy_n(src.begin(), available, out.begin());
		return available;
	}

	return 0;
}

XivAlex::SqexDef::VirtualSqPack::FileOnDiskEntryProvider::FileOnDiskEntryProvider(Utils::Win32::File file, uint64_t offset, uint32_t length) : m_file(std::move(file))
, m_offset(offset)
, m_size(length) {
}

XivAlex::SqexDef::VirtualSqPack::FileOnDiskEntryProvider::~FileOnDiskEntryProvider() = default;

uint32_t XivAlex::SqexDef::VirtualSqPack::FileOnDiskEntryProvider::Size() const {
	return m_size;
}

size_t XivAlex::SqexDef::VirtualSqPack::FileOnDiskEntryProvider::Read(uint64_t offset, void* buf, size_t length) const {
	if (offset >= m_size)
		return 0;

	return m_file.Read(m_offset + offset, buf, std::min(length, static_cast<size_t>(m_size - offset)));
}

XivAlex::SqexDef::VirtualSqPack::OnTheFlyBinaryEntryProvider::OnTheFlyBinaryEntryProvider(std::filesystem::path path)
	: m_path(std::move(path)) {
	const auto rawSize = static_cast<uint32_t>(file_size(m_path));
	const auto blockCount = (rawSize + BlockDataSize - 1) / BlockDataSize;
	m_header = {
		.HeaderSize = sizeof m_header + blockCount * sizeof SqData::BlockHeaderLocator,
		.Type = SqData::FileEntryType::Binary,
		.DecompressedSize = rawSize,
		.Unknown1 = BlockSize,
		.BlockBufferSize = BlockSize,
		.BlockCount = blockCount,
	};
	const auto align = Align(m_header.HeaderSize);
	m_padBeforeData = align.Pad;
	m_header.HeaderSize = align;
}

XivAlex::SqexDef::VirtualSqPack::OnTheFlyBinaryEntryProvider::~OnTheFlyBinaryEntryProvider() = default;

uint32_t XivAlex::SqexDef::VirtualSqPack::OnTheFlyBinaryEntryProvider::Size() const {
	if (!m_header.BlockCount)
		return m_header.HeaderSize;

	return m_header.HeaderSize +
		m_header.DecompressedSize +
		(m_header.BlockCount - 1) * BlockPadSize +  // full blocks, up to the one before the last block
		Align(m_header.DecompressedSize % BlockDataSize).Pad;  // the last block
}

size_t XivAlex::SqexDef::VirtualSqPack::OnTheFlyBinaryEntryProvider::Read(const uint64_t offset, void* const buf, const size_t length) const {
	if (!length)
		return 0;

	auto relativeOffset = offset;
	auto out = std::span(static_cast<char*>(buf), length);

	if (relativeOffset < sizeof m_header) {
		const auto src = std::span(reinterpret_cast<const char*>(&m_header), sizeof m_header)
			.subspan(static_cast<size_t>(relativeOffset));
		const auto available = std::min(out.size_bytes(), src.size_bytes());
		std::copy_n(src.begin(), available, out.begin());
		out = out.subspan(available);
		relativeOffset = 0;

		if (out.empty())
			return length - out.size_bytes();
	} else
		relativeOffset -= sizeof m_header;

	if (relativeOffset < m_header.BlockCount * sizeof SqData::BlockHeaderLocator) {
		auto i = relativeOffset / sizeof SqData::BlockHeaderLocator;
		relativeOffset -= i * sizeof SqData::BlockHeaderLocator;
		for (; i < m_header.BlockCount; ++i) {
			const auto decompressedDataSize = i == m_header.BlockCount - 1 ? m_header.DecompressedSize % BlockDataSize : BlockDataSize;
			const auto locator = SqData::BlockHeaderLocator{
				static_cast<uint32_t>(i * BlockSize),
				static_cast<uint16_t>(BlockSize),
				static_cast<uint16_t>(decompressedDataSize)
			};

			if (relativeOffset < sizeof locator) {
				const auto src = std::span(reinterpret_cast<const char*>(&locator), sizeof locator)
					.subspan(static_cast<size_t>(relativeOffset));
				const auto available = std::min(out.size_bytes(), src.size_bytes());
				std::copy_n(src.begin(), available, out.begin());
				out = out.subspan(available);
				relativeOffset = 0;

				if (out.empty())
					return length - out.size_bytes();
			} else
				relativeOffset -= sizeof locator;
		}
	} else
		relativeOffset -= m_header.BlockCount * sizeof SqData::BlockHeaderLocator;

	if (relativeOffset < m_padBeforeData) {
		const auto available = std::min(out.size_bytes(), static_cast<size_t>(m_padBeforeData - relativeOffset));
		std::fill_n(out.begin(), available, 0);
		out = out.subspan(available);
		relativeOffset = 0;

		if (out.empty())
			return length - out.size_bytes();
	} else
		relativeOffset -= m_padBeforeData;

	if (static_cast<uint32_t>(relativeOffset) < m_header.BlockCount * BlockSize) {
		auto i = relativeOffset / BlockSize;
		relativeOffset -= i * BlockSize;
		for (; i < m_header.BlockCount; ++i) {
			const auto decompressedSize = i == m_header.BlockCount - 1 ? m_header.DecompressedSize % BlockDataSize : BlockDataSize;

			if (relativeOffset < sizeof SqData::BlockHeader) {
				const auto header = SqData::BlockHeader{
					.HeaderSize = sizeof SqData::BlockHeader,
					.Version = 0,
					.CompressedSize = SqData::BlockHeader::CompressedSizeNotCompressed,
					.DecompressedSize = decompressedSize,
				};
				const auto src = std::span(reinterpret_cast<const char*>(&header), sizeof header)
					.subspan(static_cast<size_t>(relativeOffset));
				const auto available = std::min(out.size_bytes(), src.size_bytes());
				std::copy_n(src.begin(), available, out.begin());
				out = out.subspan(available);
				relativeOffset = 0;

				if (out.empty())
					return length - out.size_bytes();
			} else
				relativeOffset -= sizeof SqData::BlockHeader;

			if (relativeOffset < decompressedSize) {
				if (!m_hFile)
					m_hFile = Utils::Win32::File::Create(m_path, GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING, 0);

				const auto available = std::min(out.size_bytes(), static_cast<size_t>(decompressedSize - relativeOffset));
				m_hFile.Read(i * BlockDataSize + relativeOffset, &out[0], available);
				out = out.subspan(available);
				relativeOffset = 0;

				if (out.empty())
					return length - out.size_bytes();
			} else
				relativeOffset -= decompressedSize;

			if (const auto padSize = Align(decompressedSize + sizeof SqData::BlockHeader).Pad;
				relativeOffset < padSize) {
				const auto available = std::min(out.size_bytes(), static_cast<size_t>(padSize - relativeOffset));
				std::fill_n(out.begin(), available, 0);
				out = out.subspan(static_cast<size_t>(available));
				relativeOffset = 0;

				if (out.empty())
					return length - out.size_bytes();
			} else
				relativeOffset -= padSize;
		}
	}

	return length - out.size_bytes();
}

XivAlex::SqexDef::VirtualSqPack::MemoryBinaryEntryProvider::MemoryBinaryEntryProvider(const std::filesystem::path & path) {
	const auto file = Utils::Win32::File::Create(path, GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING, 0);

	const auto rawSize = static_cast<uint32_t>(file.Length());
	SqData::FileEntryHeader entryHeader = {
		.HeaderSize = sizeof entryHeader,
		.Type = SqData::FileEntryType::Binary,
		.DecompressedSize = rawSize,
		.Unknown1 = BlockSize,
		.BlockBufferSize = BlockSize,
		.BlockCount = 0,
	};

	std::vector<std::vector<uint8_t>> blocks;
	std::vector<SqData::BlockHeaderLocator> locators;
	for (uint32_t i = 0; i < rawSize; i += BlockDataSize) {
		uint8_t buf[BlockDataSize];
		const auto len = std::min<uint32_t>(BlockDataSize, rawSize - i);
		file.Read(i, buf, len);
		auto compressed = Utils::ZlibCompress(buf, len, Z_BEST_COMPRESSION, Z_DEFLATED, -15);

		SqData::BlockHeader header{
			.HeaderSize = sizeof SqData::BlockHeader,
			.Version = 0,
			.CompressedSize = static_cast<uint32_t>(compressed.size()),
			.DecompressedSize = len,
		};
		const auto pad = Align(sizeof header + compressed.size()).Pad;

		locators.emplace_back(SqData::BlockHeaderLocator{
			locators.empty() ? 0 : locators.back().BlockSize + locators.back().Offset,
			static_cast<uint16_t>(sizeof SqData::BlockHeader + compressed.size() + pad),
			static_cast<uint16_t>(len)
			});

		blocks.emplace_back(std::vector<uint8_t>{ reinterpret_cast<uint8_t*>(&header), reinterpret_cast<uint8_t*>(&header + 1) });
		blocks.emplace_back(std::move(compressed));
		if (pad)
			blocks.emplace_back(std::vector<uint8_t>(pad, 0));
	}

	entryHeader.BlockCount = static_cast<uint32_t>(locators.size());
	entryHeader.HeaderSize = static_cast<uint32_t>(Align(entryHeader.HeaderSize + std::span(locators).size_bytes()));
	m_data.insert(m_data.end(), reinterpret_cast<char*>(&entryHeader), reinterpret_cast<char*>(&entryHeader + 1));
	if (!locators.empty()) {
		m_data.insert(m_data.end(), reinterpret_cast<char*>(&locators.front()), reinterpret_cast<char*>(&locators.back() + 1));
		m_data.resize(entryHeader.HeaderSize, 0);
		for (const auto& block : blocks)
			m_data.insert(m_data.end(), block.begin(), block.end());
	} else
		m_data.resize(entryHeader.HeaderSize, 0);
}

XivAlex::SqexDef::VirtualSqPack::MemoryBinaryEntryProvider::~MemoryBinaryEntryProvider() = default;

uint32_t XivAlex::SqexDef::VirtualSqPack::MemoryBinaryEntryProvider::Size() const {
	return static_cast<uint32_t>(m_data.size());
}

size_t XivAlex::SqexDef::VirtualSqPack::MemoryBinaryEntryProvider::Read(const uint64_t offset, void* const buf, const size_t length) const {
	const auto available = std::min(length, static_cast<size_t>(m_data.size() - offset));
	if (!available)
		return 0;

	memcpy(buf, &m_data[static_cast<size_t>(offset)], available);
	return available;
}

XivAlex::SqexDef::VirtualSqPack::OnTheFlyModelEntryProvider::OnTheFlyModelEntryProvider(const std::filesystem::path & path)
	: m_hFile(Utils::Win32::File::Create(path, GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING, 0)) {
	const auto fileHeader = m_hFile.Read< SqData::ModelHeader>(0);
	auto baseFileOffset = static_cast<uint32_t>(sizeof fileHeader);
	m_header.Entry.Type = SqData::FileEntryType::Model;
	m_header.Entry.DecompressedSize = static_cast<uint32_t>(m_hFile.Length());
	m_header.Entry.Unknown1 = 0;
	m_header.Entry.BlockBufferSize = BlockSize;
	m_header.Entry.Version = fileHeader.Version;

	m_header.Model.VertexDeclarationCount = fileHeader.VertexDeclarationCount;
	m_header.Model.MaterialCount = fileHeader.MaterialCount;
	m_header.Model.LodCount = fileHeader.LodCount;
	m_header.Model.EnableIndexBufferStreaming = fileHeader.EnableIndexBufferStreaming;
	m_header.Model.EnableEdgeGeometry = fileHeader.EnableEdgeGeometry;

	m_header.Model.DecompressedSizes.Stack = Align(fileHeader.StackSize);
	m_header.Model.FirstBlockOffsets.Stack = NextBlockOffset();
	m_header.Model.FirstBlockIndices.Stack = static_cast<uint16_t>(m_blockOffsets.size());
	m_header.Model.BlockCount.Stack = Align<uint32_t, uint16_t>(fileHeader.StackSize.Value(), BlockDataSize).Count;
	for (uint32_t i = 0; i < m_header.Model.BlockCount.Stack; ++i) {
		m_blockOffsets.push_back(NextBlockOffset());
		m_blockDataSizes.push_back(i == m_header.Model.BlockCount.Stack - 1 ? fileHeader.StackSize % BlockDataSize : BlockDataSize);
		m_paddedBlockSizes.push_back(static_cast<uint32_t>(Align(sizeof SqData::BlockHeader + m_blockDataSizes.back())));
		m_actualFileOffsets.push_back(baseFileOffset + i * BlockDataSize);
	}
	m_header.Model.ChunkSizes.Stack = NextBlockOffset() - m_header.Model.FirstBlockOffsets.Stack;
	baseFileOffset += fileHeader.StackSize;

	m_header.Model.DecompressedSizes.Runtime = Align(fileHeader.RuntimeSize);
	m_header.Model.FirstBlockOffsets.Runtime = NextBlockOffset();
	m_header.Model.FirstBlockIndices.Runtime = static_cast<uint16_t>(m_blockOffsets.size());
	m_header.Model.BlockCount.Runtime = Align<uint32_t, uint16_t>(fileHeader.RuntimeSize.Value(), BlockDataSize).Count;
	for (uint32_t i = 0; i < m_header.Model.BlockCount.Runtime; ++i) {
		m_blockOffsets.push_back(NextBlockOffset());
		m_blockDataSizes.push_back(i == m_header.Model.BlockCount.Runtime - 1 ? fileHeader.RuntimeSize % BlockDataSize : BlockDataSize);
		m_paddedBlockSizes.push_back(static_cast<uint32_t>(Align(sizeof SqData::BlockHeader + m_blockDataSizes.back())));
		m_actualFileOffsets.push_back(baseFileOffset + i * BlockDataSize);
	}
	m_header.Model.ChunkSizes.Runtime = NextBlockOffset() - m_header.Model.FirstBlockOffsets.Runtime;
	baseFileOffset += fileHeader.RuntimeSize;

	for (size_t i = 0; i < 3; i++) {
		m_header.Model.DecompressedSizes.Vertex[i] = Align(fileHeader.VertexSize[i]);
		m_header.Model.FirstBlockOffsets.Vertex[i] = NextBlockOffset();
		m_header.Model.FirstBlockIndices.Vertex[i] = static_cast<uint16_t>(m_blockOffsets.size());
		m_header.Model.BlockCount.Vertex[i] = Align<uint32_t, uint16_t>(fileHeader.VertexSize[i].Value(), BlockDataSize).Count;
		for (uint32_t j = 0; j < m_header.Model.BlockCount.Vertex[i]; ++j) {
			m_blockOffsets.push_back(NextBlockOffset());
			m_blockDataSizes.push_back(j == m_header.Model.BlockCount.Vertex[i] - 1 ? fileHeader.VertexSize[i] % BlockDataSize : BlockDataSize);
			m_paddedBlockSizes.push_back(static_cast<uint32_t>(Align(sizeof SqData::BlockHeader + m_blockDataSizes.back())));
			m_actualFileOffsets.push_back(baseFileOffset + j * BlockDataSize);
		}
		m_header.Model.ChunkSizes.Vertex[i] = NextBlockOffset() - m_header.Model.FirstBlockOffsets.Vertex[i];
		baseFileOffset += fileHeader.VertexSize[i];

		const auto edgeGeometryVertexSize = fileHeader.IndexOffset[i] - baseFileOffset;
		m_header.Model.DecompressedSizes.EdgeGeometryVertex[i] = Align(edgeGeometryVertexSize);
		m_header.Model.FirstBlockOffsets.EdgeGeometryVertex[i] = NextBlockOffset();
		m_header.Model.FirstBlockIndices.EdgeGeometryVertex[i] = static_cast<uint16_t>(m_blockOffsets.size());
		m_header.Model.BlockCount.EdgeGeometryVertex[i] = Align<uint32_t, uint16_t>(edgeGeometryVertexSize, BlockDataSize).Count;
		for (uint32_t j = 0; j < m_header.Model.BlockCount.EdgeGeometryVertex[i]; ++j) {
			m_blockOffsets.push_back(NextBlockOffset());
			m_blockDataSizes.push_back(j == m_header.Model.BlockCount.EdgeGeometryVertex[i] - 1 ? edgeGeometryVertexSize % BlockDataSize : BlockDataSize);
			m_paddedBlockSizes.push_back(static_cast<uint32_t>(Align(sizeof SqData::BlockHeader + m_blockDataSizes.back())));
			m_actualFileOffsets.push_back(baseFileOffset + j * BlockDataSize);
		}
		m_header.Model.ChunkSizes.EdgeGeometryVertex[i] = NextBlockOffset() - m_header.Model.FirstBlockOffsets.EdgeGeometryVertex[i];
		baseFileOffset += edgeGeometryVertexSize;

		m_header.Model.DecompressedSizes.Index[i] = Align(fileHeader.IndexSize[i]);
		m_header.Model.FirstBlockOffsets.Index[i] = NextBlockOffset();
		m_header.Model.FirstBlockIndices.Index[i] = static_cast<uint16_t>(m_blockOffsets.size());
		m_header.Model.BlockCount.Index[i] = Align<uint32_t, uint16_t>(fileHeader.IndexSize[i].Value(), BlockDataSize).Count;
		for (uint32_t j = 0; j < m_header.Model.BlockCount.Index[i]; ++j) {
			m_blockOffsets.push_back(NextBlockOffset());
			m_blockDataSizes.push_back(j == m_header.Model.BlockCount.Index[i] - 1 ? fileHeader.IndexSize[i] % BlockDataSize : BlockDataSize);
			m_paddedBlockSizes.push_back(static_cast<uint32_t>(Align(sizeof SqData::BlockHeader + m_blockDataSizes.back())));
			m_actualFileOffsets.push_back(baseFileOffset + j * BlockDataSize);
		}
		m_header.Model.ChunkSizes.Index[i] = NextBlockOffset() - m_header.Model.FirstBlockOffsets.Index[i];
		baseFileOffset += fileHeader.IndexSize[i];
	}

	if (baseFileOffset > m_header.Entry.DecompressedSize)
		throw std::runtime_error("Bad model file (incomplete data)");

	m_header.Entry.HeaderSize = Align(static_cast<uint32_t>(sizeof ModelEntryHeader + std::span(m_blockDataSizes).size_bytes()));
}

XivAlex::SqexDef::VirtualSqPack::OnTheFlyModelEntryProvider::~OnTheFlyModelEntryProvider() = default;

XivAlex::SqexDef::AlignResult<unsigned> XivAlex::SqexDef::VirtualSqPack::OnTheFlyModelEntryProvider::AlignEntry() const {
	return Align(m_header.Entry.HeaderSize + (m_paddedBlockSizes.empty() ? 0 : m_blockOffsets.back() + m_paddedBlockSizes.back()));
}

uint32_t XivAlex::SqexDef::VirtualSqPack::OnTheFlyModelEntryProvider::NextBlockOffset() const {
	return m_paddedBlockSizes.empty() ? 0U : Align(m_blockOffsets.back() + m_paddedBlockSizes.back());
}

uint32_t XivAlex::SqexDef::VirtualSqPack::OnTheFlyModelEntryProvider::Size() const {
	return AlignEntry();
}

size_t XivAlex::SqexDef::VirtualSqPack::OnTheFlyModelEntryProvider::Read(uint64_t offset, void* buf, size_t length) const {
	if (!length)
		return 0;

	auto relativeOffset = offset;
	auto out = std::span(static_cast<char*>(buf), length);

	if (relativeOffset < sizeof m_header) {
		const auto src = std::span(reinterpret_cast<const char*>(&m_header), sizeof m_header)
			.subspan(static_cast<size_t>(relativeOffset));
		const auto available = std::min(out.size_bytes(), src.size_bytes());
		std::copy_n(src.begin(), available, out.begin());
		out = out.subspan(available);
		relativeOffset = 0;

		if (out.empty())
			return length - out.size_bytes();
	} else
		relativeOffset -= sizeof m_header;

	if (const auto srcTyped = std::span(m_paddedBlockSizes);
		relativeOffset < srcTyped.size_bytes()) {
		const auto src = std::span(reinterpret_cast<const char*>(srcTyped.data()), srcTyped.size_bytes())
			.subspan(static_cast<size_t>(relativeOffset));
		const auto available = std::min(out.size_bytes(), src.size_bytes());
		std::copy_n(src.begin(), available, out.begin());
		out = out.subspan(available);
		relativeOffset = 0;

		if (out.empty())
			return length - out.size_bytes();
	} else
		relativeOffset -= srcTyped.size_bytes();

	if (const auto padBeforeBlocks = Align(sizeof ModelEntryHeader + std::span(m_paddedBlockSizes).size_bytes()).Pad;
		relativeOffset < padBeforeBlocks) {
		const auto available = std::min(out.size_bytes(), padBeforeBlocks - relativeOffset);
		std::fill_n(out.begin(), available, 0);
		out = out.subspan(available);
		relativeOffset = 0;

		if (out.empty())
			return length - out.size_bytes();
	} else
		relativeOffset -= padBeforeBlocks;

	auto it = std::lower_bound(m_blockOffsets.begin(), m_blockOffsets.end(),
		static_cast<uint32_t>(relativeOffset),
		[&](uint32_t l, uint32_t r) {
		return l < r;
	});

	if (it == m_blockOffsets.end())
		--it;
	while (*it > relativeOffset) {
		if (it == m_blockOffsets.begin()) {
			const auto available = std::min(out.size_bytes(), *it - relativeOffset);
			std::fill_n(out.begin(), available, 0);
			out = out.subspan(available);
			relativeOffset = 0;

			if (out.empty())
				return length - out.size_bytes();
			break;
		} else
			--it;
	}

	relativeOffset -= *it;

	for (auto i = it - m_blockOffsets.begin(); it != m_blockOffsets.end(); ++it, ++i) {
		if (relativeOffset < sizeof SqData::BlockHeader) {
			const auto header = SqData::BlockHeader{
				.HeaderSize = sizeof SqData::BlockHeader,
				.Version = 0,
				.CompressedSize = SqData::BlockHeader::CompressedSizeNotCompressed,
				.DecompressedSize = m_blockDataSizes[i],
			};
			const auto src = std::span(reinterpret_cast<const char*>(&header), sizeof header)
				.subspan(static_cast<size_t>(relativeOffset));
			const auto available = std::min(out.size_bytes(), src.size_bytes());
			std::copy_n(src.begin(), available, out.begin());
			out = out.subspan(available);
			relativeOffset = 0;

			if (out.empty())
				return length - out.size_bytes();
		} else
			relativeOffset -= sizeof SqData::BlockHeader;

		if (relativeOffset < m_blockDataSizes[i]) {
			const auto available = std::min(out.size_bytes(), static_cast<size_t>(m_blockDataSizes[i] - relativeOffset));
			m_hFile.Read(m_actualFileOffsets[i] + relativeOffset, &out[0], available);
			out = out.subspan(available);
			relativeOffset = 0;

			if (out.empty())
				return length - out.size_bytes();
		} else
			relativeOffset -= m_blockDataSizes[i];

		if (const auto padSize = m_paddedBlockSizes[i] - m_blockDataSizes[i] - sizeof SqData::BlockHeader;
			relativeOffset < padSize) {
			const auto available = std::min(out.size_bytes(), static_cast<size_t>(padSize - relativeOffset));
			std::fill_n(out.begin(), available, 0);
			out = out.subspan(static_cast<size_t>(available));
			relativeOffset = 0;

			if (out.empty())
				return length - out.size_bytes();
		} else
			relativeOffset -= padSize;
	}

	if (const auto endPadding = AlignEntry().Pad;
		relativeOffset < endPadding) {
		const auto available = std::min(out.size_bytes(), endPadding - relativeOffset);
		std::fill_n(out.begin(), available, 0);
		out = out.subspan(available);
	}

	return length - out.size_bytes();
}

const XivAlex::SqexDef::SqData::TexHeader& XivAlex::SqexDef::VirtualSqPack::OnTheFlyTextureEntryProvider::AsTexHeader() const {
	return *reinterpret_cast<const SqData::TexHeader*>(&m_texHeaderBytes[0]);
}

std::span<const uint32_t> XivAlex::SqexDef::VirtualSqPack::OnTheFlyTextureEntryProvider::AsMipmapOffsets() const {
	return std::span(
		reinterpret_cast<const uint32_t*>(&m_texHeaderBytes[sizeof SqData::TexHeader]),
		AsTexHeader().MipmapCount
	);
}

XivAlex::SqexDef::VirtualSqPack::OnTheFlyTextureEntryProvider::OnTheFlyTextureEntryProvider(std::filesystem::path path)
	: m_hFile(Utils::Win32::File::Create(path, GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING, 0)) {

	auto entryHeader = SqData::FileEntryHeader{
		.HeaderSize = sizeof SqData::FileEntryHeader,
		.Type = SqData::FileEntryType::Texture,
		.DecompressedSize = static_cast<uint32_t>(m_hFile.Length()),
		.Unknown1 = 0,
		.BlockBufferSize = BlockSize,
	};

	m_texHeaderBytes.resize(sizeof SqData::TexHeader);
	m_hFile.Read(0, std::span(m_texHeaderBytes));
	entryHeader.BlockCount = AsTexHeader().MipmapCount;

	m_texHeaderBytes.resize(sizeof SqData::TexHeader + AsTexHeader().MipmapCount * sizeof uint32_t);
	m_hFile.Read(sizeof SqData::TexHeader, std::span(
		reinterpret_cast<uint32_t*>(&m_texHeaderBytes[sizeof SqData::TexHeader]),
		AsTexHeader().MipmapCount.Value()
	));

	m_texHeaderBytes.resize(AsMipmapOffsets()[0]);
	m_hFile.Read(0, std::span(m_texHeaderBytes).subspan(0, AsMipmapOffsets()[0]));

	const auto mipmapOffsets = AsMipmapOffsets();
	for (const auto offset : mipmapOffsets) {
		if (!m_mipmapSizes.empty())
			m_mipmapSizes.back() = offset - m_mipmapSizes.back();
		m_mipmapSizes.push_back(offset);
	}
	m_mipmapSizes.back() = entryHeader.DecompressedSize - m_mipmapSizes.back();

	uint32_t blockOffsetCounter = 0;
	for (size_t i = 0; i < mipmapOffsets.size(); ++i) {
		const auto mipmapSize = m_mipmapSizes[i];
		const auto subBlockCount = (mipmapSize + BlockDataSize - 1) / BlockDataSize;
		SqData::TextureBlockHeaderLocator loc{
			blockOffsetCounter,
			(subBlockCount * sizeof SqData::BlockHeader + mipmapSize),
			mipmapSize,
			i == 0 ? 0 : m_blockLocators.back().FirstSubBlockIndex + m_blockLocators.back().SubBlockCount,
			subBlockCount,
		};
		for (size_t j = 0; j < subBlockCount; ++j) {
			m_subBlockSizes.push_back(
				sizeof SqData::BlockHeader +
				(j == subBlockCount - 1 ? mipmapSize % BlockDataSize : BlockDataSize)
			);
			blockOffsetCounter += sizeof SqData::BlockHeader + m_subBlockSizes.back();
		}

		m_blockLocators.emplace_back(loc);
	}
	for (auto& loc : m_blockLocators) {
		loc.FirstBlockOffset = loc.FirstBlockOffset + static_cast<uint32_t>(std::span(m_texHeaderBytes).size_bytes());
	}

	entryHeader.HeaderSize = entryHeader.HeaderSize + static_cast<uint32_t>(
		std::span(m_blockLocators).size_bytes() +
		std::span(m_subBlockSizes).size_bytes());

	m_mergedHeader.insert(m_mergedHeader.end(),
		reinterpret_cast<char*>(&entryHeader),
		reinterpret_cast<char*>(&entryHeader + 1));
	m_mergedHeader.insert(m_mergedHeader.end(),
		reinterpret_cast<char*>(&m_blockLocators.front()),
		reinterpret_cast<char*>(&m_blockLocators.back() + 1));
	m_mergedHeader.insert(m_mergedHeader.end(),
		reinterpret_cast<char*>(&m_subBlockSizes.front()),
		reinterpret_cast<char*>(&m_subBlockSizes.back() + 1));
	m_mergedHeader.insert(m_mergedHeader.end(),
		m_texHeaderBytes.begin(),
		m_texHeaderBytes.end());

	m_size += m_mergedHeader.size();
	for (const auto mipmapSize : m_mipmapSizes) {
		m_size += (mipmapSize + BlockDataSize - 1) / BlockDataSize * sizeof SqData::BlockHeader + mipmapSize;
	}
}

XivAlex::SqexDef::VirtualSqPack::OnTheFlyTextureEntryProvider::~OnTheFlyTextureEntryProvider() = default;

uint32_t XivAlex::SqexDef::VirtualSqPack::OnTheFlyTextureEntryProvider::Size() const {
	return static_cast<uint32_t>(m_size);
}

size_t XivAlex::SqexDef::VirtualSqPack::OnTheFlyTextureEntryProvider::Read(const uint64_t offset, void* const buf, const size_t length) const {
	if (!length)
		return 0;

	auto relativeOffset = offset;
	auto out = std::span(static_cast<char*>(buf), length);

	if (relativeOffset < m_mergedHeader.size()) {
		const auto src = std::span(m_mergedHeader)
			.subspan(static_cast<size_t>(relativeOffset));
		const auto available = std::min(out.size_bytes(), src.size_bytes());
		std::copy_n(src.begin(), available, out.begin());
		out = out.subspan(available);
		relativeOffset = 0;

		if (out.empty())
			return length - out.size_bytes();
	} else
		relativeOffset -= m_mergedHeader.size();

	if (relativeOffset < m_size) {
		relativeOffset += std::span(m_texHeaderBytes).size_bytes();
		auto it = std::lower_bound(m_blockLocators.begin(), m_blockLocators.end(),
			SqData::TextureBlockHeaderLocator{ .FirstBlockOffset = static_cast<uint32_t>(relativeOffset) },
			[&](const SqData::TextureBlockHeaderLocator& l, const SqData::TextureBlockHeaderLocator& r) {
			return l.FirstBlockOffset < r.FirstBlockOffset;
		});

		if (it == m_blockLocators.end())
			--it;
		while (it->FirstBlockOffset > relativeOffset) {
			if (it == m_blockLocators.begin()) {
				const auto available = std::min(out.size_bytes(), it->FirstBlockOffset - relativeOffset);
				std::fill_n(out.begin(), available, 0);
				out = out.subspan(available);
				relativeOffset = 0;

				if (out.empty())
					return length - out.size_bytes();
				break;
			} else
				--it;
		}

		relativeOffset -= it->FirstBlockOffset;

		for (; it != m_blockLocators.end(); ++it) {
			const auto blockIndex = it - m_blockLocators.begin();
			auto j = relativeOffset / (sizeof SqData::BlockHeader + BlockDataSize);
			relativeOffset -= j * (sizeof SqData::BlockHeader + BlockDataSize);
			for (; j < it->SubBlockCount; ++j) {
				const auto decompressedSize = j == it->SubBlockCount - 1 ? m_mipmapSizes[blockIndex] % BlockDataSize : BlockDataSize;

				if (relativeOffset < sizeof SqData::BlockHeader) {
					const auto header = SqData::BlockHeader{
						.HeaderSize = sizeof SqData::BlockHeader,
						.Version = 0,
						.CompressedSize = SqData::BlockHeader::CompressedSizeNotCompressed,
						.DecompressedSize = decompressedSize,
					};
					const auto src = std::span(reinterpret_cast<const char*>(&header), sizeof header)
						.subspan(static_cast<size_t>(relativeOffset));
					const auto available = std::min(out.size_bytes(), src.size_bytes());
					std::copy_n(src.begin(), available, out.begin());
					out = out.subspan(available);
					relativeOffset = 0;

					if (out.empty())
						return length - out.size_bytes();
				} else
					relativeOffset -= sizeof SqData::BlockHeader;

				if (relativeOffset < decompressedSize) {
					const auto available = std::min(out.size_bytes(), static_cast<size_t>(decompressedSize - relativeOffset));
					m_hFile.Read(AsMipmapOffsets()[blockIndex] + j * BlockDataSize + relativeOffset, &out[0], available);
					out = out.subspan(available);
					relativeOffset = 0;

					if (out.empty())
						return length - out.size_bytes();
				} else
					relativeOffset -= decompressedSize;
			}
		}
	}

	return length - out.size_bytes();
}

XivAlex::SqexDef::VirtualSqPack::VirtualSqPack() = default;

XivAlex::SqexDef::VirtualSqPack::AddEntryResult& XivAlex::SqexDef::VirtualSqPack::AddEntryResult::operator+=(const AddEntryResult & r) {
	AddedCount += r.AddedCount;
	ReplacedCount += r.ReplacedCount;
	return *this;
}

XivAlex::SqexDef::VirtualSqPack::AddEntryResult XivAlex::SqexDef::VirtualSqPack::AddEntry(uint32_t PathHash, uint32_t NameHash, uint32_t FullPathHash, std::unique_ptr<EntryProvider> provider) {
	if (m_frozen)
		throw std::runtime_error("Trying to operate on a frozen VirtualSqPack");

	if (PathHash != Entry::NoEntryHash || NameHash != Entry::NoEntryHash) {
		const auto it = m_pathNameTupleEntryPointerMap.find(std::make_pair(PathHash, NameHash));
		if (it != m_pathNameTupleEntryPointerMap.end()) {
			it->second->Provider = std::move(provider);
			if (FullPathHash != Entry::NoEntryHash) {
				m_fullPathEntryPointerMap.erase(it->second->FullPathHash);
				it->second->FullPathHash = FullPathHash;
				m_fullPathEntryPointerMap.insert_or_assign(FullPathHash, it->second);
				return { 0, 1 };
			}
		}
	}
	if (FullPathHash != Entry::NoEntryHash) {
		const auto it = m_fullPathEntryPointerMap.find(FullPathHash);
		if (it != m_fullPathEntryPointerMap.end()) {
			it->second->Provider = std::move(provider);
			if (PathHash != Entry::NoEntryHash || NameHash != Entry::NoEntryHash) {
				m_pathNameTupleEntryPointerMap.erase(std::make_pair(it->second->PathHash, it->second->NameHash));
				it->second->PathHash = PathHash;
				it->second->NameHash = NameHash;
				m_pathNameTupleEntryPointerMap.insert_or_assign(std::make_pair(PathHash, NameHash), it->second);
				return { 0, 1 };
			}
		}
	}

	auto entry = std::make_unique<Entry>(PathHash, NameHash, FullPathHash, 0, 0, 0, 0, SqIndex::LEDataLocator{ 0, 0 }, std::move(provider));
	const auto ptr = entry.get();
	m_entries.emplace_back(std::move(entry));
	if (FullPathHash != Entry::NoEntryHash)
		m_fullPathEntryPointerMap.insert_or_assign(FullPathHash, ptr);
	if (PathHash != Entry::NoEntryHash || NameHash != Entry::NoEntryHash)
		m_pathNameTupleEntryPointerMap.insert_or_assign(std::make_pair(PathHash, NameHash), ptr);
	return { 1, 0 };
}

XivAlex::SqexDef::VirtualSqPack::AddEntryResult XivAlex::SqexDef::VirtualSqPack::AddEntriesFromSqPack(const std::filesystem::path & indexPath, bool overwriteUnknownSegments) {
	if (m_frozen)
		throw std::runtime_error("Trying to operate on a frozen VirtualSqPack");

	FileSystemSqPack m_original{ indexPath, false };

	AddEntryResult result{};

	if (overwriteUnknownSegments) {
		m_sqpackIndexSegment2 = std::move(m_original.Index.DataFileSegment);
		m_sqpackIndexSegment3 = std::move(m_original.Index.Segment3);
		m_sqpackIndex2Segment2 = std::move(m_original.Index2.DataFileSegment);
		m_sqpackIndex2Segment3 = std::move(m_original.Index2.Segment3);
	}

	std::vector<size_t> openFileIndexMap;
	for (auto& f : m_original.Data) {
		const auto curItemPath = f.FileOnDisk.ResolveName();
		size_t found;
		for (found = 0; found < m_openFiles.size(); ++found) {
			if (equivalent(m_openFiles[found].ResolveName(), curItemPath)) {
				break;
			}
		}
		if (found == m_openFiles.size()) {
			m_openFiles.emplace_back(std::move(f.FileOnDisk));
		}
		openFileIndexMap.push_back(found);
	}

	for (const auto& entry : m_original.Files) {
		result += AddEntry(entry.IndexEntry.PathHash, entry.IndexEntry.NameHash, entry.Index2Entry.FullPathHash,
			std::make_unique<FileOnDiskEntryProvider>(
				Utils::Win32::File{ m_openFiles[openFileIndexMap[entry.DataFileIndex]], false },
				entry.DataEntryOffset, entry.DataEntrySize));
	}

	return result;
}

XivAlex::SqexDef::VirtualSqPack::AddEntryResult XivAlex::SqexDef::VirtualSqPack::AddEntryFromFile(uint32_t PathHash, uint32_t NameHash, uint32_t FullPathHash, const std::filesystem::path & path) {
	if (m_frozen)
		throw std::runtime_error("Trying to operate on a frozen VirtualSqPack");

	if (file_size(path) == 0) {
		return AddEntry(PathHash, NameHash, FullPathHash, std::make_unique<EmptyEntryProvider>());
	} else if (path.extension() == ".tex") {
		return AddEntry(PathHash, NameHash, FullPathHash, std::make_unique<OnTheFlyTextureEntryProvider>(path));
	} else if (path.extension() == ".mdl") {
		return AddEntry(PathHash, NameHash, FullPathHash, std::make_unique<OnTheFlyModelEntryProvider>(path));
	} else {
		// return AddEntry(PathHash, NameHash, FullPathHash, std::make_unique<MemoryBinaryEntryProvider>(path));
		return AddEntry(PathHash, NameHash, FullPathHash, std::make_unique<OnTheFlyBinaryEntryProvider>(path));
	}
}

size_t XivAlex::SqexDef::VirtualSqPack::NumOfDataFiles() const {
	return m_sqpackDataSubHeaders.size();
}

XivAlex::SqexDef::SqData::Header& XivAlex::SqexDef::VirtualSqPack::AllocateDataSpace(size_t length, bool strict) {
	if (m_sqpackDataSubHeaders.empty() ||
		sizeof SqpackHeader + sizeof SqData::Header + m_sqpackDataSubHeaders.back().DataSize + length > m_sqpackDataSubHeaders.back().MaxFileSize) {
		if (strict && !m_sqpackDataSubHeaders.empty()) {
			CalculateSha1(m_sqpackDataSubHeaders.back().Sha1, std::span(&m_sqpackDataSubHeaders.back(), 1));
		}
		m_sqpackDataSubHeaders.emplace_back(SqData::Header{
			.HeaderSize = sizeof SqData::Header,
			.Unknown1 = SqData::Header::Unknown1_Value,
			.DataSize = 0,
			.SpanIndex = static_cast<uint32_t>(m_sqpackDataSubHeaders.size()),
			.MaxFileSize = SqData::Header::MaxFileSize_MaxValue,
			});
	}
	return m_sqpackDataSubHeaders.back();
}

void XivAlex::SqexDef::VirtualSqPack::Freeze(bool strict) {
	if (m_frozen)
		throw std::runtime_error("Cannot freeze again");

	m_fileEntries1.clear();
	m_fileEntries2.clear();

	m_sqpackIndexSubHeader.DataFilesSegment.Count = 1;
	m_sqpackIndex2SubHeader.DataFilesSegment.Count = 1;

	for (const auto& entry : m_entries) {
		entry->BlockSize = entry->Provider->Size();
		entry->PadSize = Align(entry->BlockSize).Pad;

		auto& dataSubHeader = AllocateDataSpace(0ULL + entry->BlockSize + entry->PadSize, strict);
		entry->DataFileIndex = static_cast<uint32_t>(m_sqpackDataSubHeaders.size() - 1);
		entry->OffsetAfterHeaders = dataSubHeader.DataSize;

		dataSubHeader.DataSize = dataSubHeader.DataSize + entry->BlockSize + entry->PadSize;

		const auto dataLocator = SqIndex::LEDataLocator{
			entry->DataFileIndex,
			sizeof SqpackHeader + sizeof SqData::Header + entry->OffsetAfterHeaders,
		};
		entry->Locator = dataLocator;
		m_fileEntries1.emplace_back(SqIndex::FileSegmentEntry{ entry->NameHash, entry->PathHash, dataLocator, 0 });
		m_fileEntries2.emplace_back(SqIndex::FileSegmentEntry2{ entry->FullPathHash, dataLocator });
	}

	std::sort(m_fileEntries1.begin(), m_fileEntries1.end(), [](const SqIndex::FileSegmentEntry& l, const SqIndex::FileSegmentEntry& r) {
		if (l.PathHash == r.PathHash)
			return l.NameHash < r.NameHash;
		else
			return l.PathHash < r.PathHash;
	});
	std::sort(m_fileEntries2.begin(), m_fileEntries2.end(), [](const SqIndex::FileSegmentEntry2& l, const SqIndex::FileSegmentEntry2& r) {
		return l.FullPathHash < r.FullPathHash;
	});

	memcpy(m_sqpackIndexHeader.Signature, SqpackHeader::Signature_Value, sizeof SqpackHeader::Signature_Value);
	m_sqpackIndexHeader.HeaderSize = sizeof SqpackHeader;
	m_sqpackIndexHeader.Unknown1 = SqpackHeader::Unknown1_Value;
	m_sqpackIndexHeader.Type = SqpackType::SqIndex;
	m_sqpackIndexHeader.Unknown2 = SqpackHeader::Unknown2_Value;
	if (strict)
		CalculateSha1(m_sqpackIndexHeader.Sha1, std::span(&m_sqpackIndexHeader, 1));

	m_sqpackIndexSubHeader.HeaderSize = sizeof SqIndex::Header;
	m_sqpackIndexSubHeader.Type = SqIndex::Header::IndexType::Index;
	m_sqpackIndexSubHeader.FileSegment.Count = 1;
	m_sqpackIndexSubHeader.FileSegment.Offset = m_sqpackIndexHeader.HeaderSize + m_sqpackIndexSubHeader.HeaderSize;
	m_sqpackIndexSubHeader.FileSegment.Size = static_cast<uint32_t>(std::span(m_fileEntries1).size_bytes());
	m_sqpackIndexSubHeader.DataFilesSegment.Count = static_cast<uint32_t>(m_sqpackDataSubHeaders.size());
	m_sqpackIndexSubHeader.DataFilesSegment.Offset = m_sqpackIndexSubHeader.FileSegment.Offset + m_sqpackIndexSubHeader.FileSegment.Size;
	m_sqpackIndexSubHeader.DataFilesSegment.Size = static_cast<uint32_t>(std::span(m_sqpackIndexSegment2).size_bytes());
	m_sqpackIndexSubHeader.UnknownSegment3.Count = 0;
	m_sqpackIndexSubHeader.UnknownSegment3.Offset = m_sqpackIndexSubHeader.DataFilesSegment.Offset + m_sqpackIndexSubHeader.DataFilesSegment.Size;
	m_sqpackIndexSubHeader.UnknownSegment3.Size = static_cast<uint32_t>(std::span(m_sqpackIndexSegment3).size_bytes());
	m_sqpackIndexSubHeader.FolderSegment.Count = 0;
	m_sqpackIndexSubHeader.FolderSegment.Offset = m_sqpackIndexSubHeader.UnknownSegment3.Offset + m_sqpackIndexSubHeader.UnknownSegment3.Size;
	for (size_t i = 0; i < m_fileEntries1.size(); ++i) {
		const auto& entry = m_fileEntries1[i];
		if (m_folderEntries.empty() || m_folderEntries.back().NameHash != entry.PathHash) {
			m_folderEntries.emplace_back(
				entry.PathHash,
				static_cast<uint32_t>(m_sqpackIndexSubHeader.FileSegment.Offset + i * sizeof entry),
				static_cast<uint32_t>(sizeof entry),
				0);
		} else {
			m_folderEntries.back().FileSegmentSize = m_folderEntries.back().FileSegmentSize + sizeof entry;
		}
	}
	m_sqpackIndexSubHeader.FolderSegment.Size = static_cast<uint32_t>(std::span(m_folderEntries).size_bytes());
	if (strict)
		CalculateSha1(m_sqpackIndexSubHeader.Sha1, std::span(&m_sqpackIndexSubHeader, 1));

	memcpy(m_sqpackIndex2Header.Signature, SqpackHeader::Signature_Value, sizeof SqpackHeader::Signature_Value);
	m_sqpackIndex2Header.HeaderSize = sizeof SqpackHeader;
	m_sqpackIndex2Header.Unknown1 = SqpackHeader::Unknown1_Value;
	m_sqpackIndex2Header.Type = SqpackType::SqIndex;
	m_sqpackIndex2Header.Unknown2 = SqpackHeader::Unknown2_Value;
	if (strict)
		CalculateSha1(m_sqpackIndex2Header.Sha1, std::span(&m_sqpackIndex2Header, 1));

	m_sqpackIndex2SubHeader.HeaderSize = sizeof SqIndex::Header;
	m_sqpackIndex2SubHeader.Type = SqIndex::Header::IndexType::Index2;
	m_sqpackIndex2SubHeader.FileSegment.Count = 1;
	m_sqpackIndex2SubHeader.FileSegment.Offset = m_sqpackIndex2Header.HeaderSize + m_sqpackIndex2SubHeader.HeaderSize;
	m_sqpackIndex2SubHeader.FileSegment.Size = static_cast<uint32_t>(std::span(m_fileEntries2).size_bytes());
	m_sqpackIndex2SubHeader.DataFilesSegment.Count = static_cast<uint32_t>(m_sqpackDataSubHeaders.size());
	m_sqpackIndex2SubHeader.DataFilesSegment.Offset = m_sqpackIndex2SubHeader.FileSegment.Offset + m_sqpackIndex2SubHeader.FileSegment.Size;
	m_sqpackIndex2SubHeader.DataFilesSegment.Size = static_cast<uint32_t>(std::span(m_sqpackIndex2Segment2).size_bytes());
	m_sqpackIndex2SubHeader.UnknownSegment3.Count = 0;
	m_sqpackIndex2SubHeader.UnknownSegment3.Offset = m_sqpackIndex2SubHeader.DataFilesSegment.Offset + m_sqpackIndex2SubHeader.DataFilesSegment.Size;
	m_sqpackIndex2SubHeader.UnknownSegment3.Size = static_cast<uint32_t>(std::span(m_sqpackIndex2Segment3).size_bytes());
	m_sqpackIndex2SubHeader.FolderSegment.Count = 0;
	m_sqpackIndex2SubHeader.FolderSegment.Offset = m_sqpackIndex2SubHeader.UnknownSegment3.Offset + m_sqpackIndex2SubHeader.UnknownSegment3.Size;
	m_sqpackIndex2SubHeader.FolderSegment.Size = 0;
	if (strict)
		CalculateSha1(m_sqpackIndex2SubHeader.Sha1, std::span(&m_sqpackIndex2SubHeader, 1));

	memcpy(m_sqpackDataHeader.Signature, SqpackHeader::Signature_Value, sizeof SqpackHeader::Signature_Value);
	m_sqpackDataHeader.HeaderSize = sizeof SqpackHeader;
	m_sqpackDataHeader.Unknown1 = SqpackHeader::Unknown1_Value;
	m_sqpackDataHeader.Type = SqpackType::SqData;
	m_sqpackDataHeader.Unknown2 = SqpackHeader::Unknown2_Value;
	if (strict)
		CalculateSha1(m_sqpackDataHeader.Sha1, std::span(&m_sqpackDataHeader, 1));

	m_frozen = true;
}

size_t XivAlex::SqexDef::VirtualSqPack::ReadIndex1(const uint64_t offset, void* const buf, const size_t length) const {
	if (!m_frozen)
		throw std::runtime_error("Trying to operate on a non frozen VirtualSqPack");
	if (!length)
		return 0;

	auto relativeOffset = offset;
	auto out = std::span(static_cast<char*>(buf), length);

	for (const auto& [ptr, cb] : std::initializer_list<std::tuple<const void*, size_t>>{
		{&m_sqpackIndexHeader, sizeof m_sqpackIndexHeader},
		{&m_sqpackIndexSubHeader, sizeof m_sqpackIndexSubHeader},
		{m_fileEntries1.data(), std::span(m_fileEntries1).size_bytes()},
		{m_sqpackIndexSegment2.data(), std::span(m_sqpackIndexSegment2).size_bytes()},
		{m_sqpackIndexSegment3.data(), std::span(m_sqpackIndexSegment3).size_bytes()},
		{m_folderEntries.data(), std::span(m_folderEntries).size_bytes()},
		}) {
		if (relativeOffset < cb) {
			const auto src = std::span(static_cast<const char*>(ptr), cb)
				.subspan(static_cast<size_t>(relativeOffset));
			const auto available = std::min(out.size_bytes(), src.size_bytes());
			std::copy_n(src.begin(), available, out.begin());
			out = out.subspan(available);
			relativeOffset = 0;
		} else
			relativeOffset -= cb;

		if (out.empty())
			return length - out.size_bytes();
	}

	return length - out.size_bytes();
}

size_t XivAlex::SqexDef::VirtualSqPack::ReadIndex2(const uint64_t offset, void* const buf, const size_t length) const {
	if (!m_frozen)
		throw std::runtime_error("Trying to operate on a non frozen VirtualSqPack");
	if (!length)
		return 0;

	auto relativeOffset = offset;
	auto out = std::span(static_cast<char*>(buf), length);

	for (const auto& [ptr, cb] : std::initializer_list<std::tuple<const void*, size_t>>{
		{&m_sqpackIndex2Header, sizeof m_sqpackIndex2Header},
		{&m_sqpackIndex2SubHeader, sizeof m_sqpackIndex2SubHeader},
		{m_fileEntries2.data(), std::span(m_fileEntries2).size_bytes()},
		{m_sqpackIndex2Segment2.data(), std::span(m_sqpackIndex2Segment2).size_bytes()},
		{m_sqpackIndex2Segment3.data(), std::span(m_sqpackIndex2Segment3).size_bytes()},
		}) {
		if (relativeOffset < cb) {
			const auto src = std::span(static_cast<const char*>(ptr), cb)
				.subspan(static_cast<size_t>(relativeOffset));
			const auto available = std::min(out.size_bytes(), src.size_bytes());
			std::copy_n(src.begin(), available, out.begin());
			out = out.subspan(available);
			relativeOffset = 0;
		} else
			relativeOffset -= cb;

		if (out.empty())
			return length - out.size_bytes();
	}

	return length - out.size_bytes();
}

size_t XivAlex::SqexDef::VirtualSqPack::ReadData(uint32_t datIndex, const uint64_t offset, void* const buf, const size_t length) const {
	if (!m_frozen)
		throw std::runtime_error("Trying to operate on a non frozen VirtualSqPack");
	if (!length)
		return 0;

	auto relativeOffset = offset;
	auto out = std::span(static_cast<char*>(buf), length);

	for (const auto& [ptr, cb] : std::initializer_list<std::tuple<const void*, size_t>>{
		{&m_sqpackDataHeader, sizeof m_sqpackDataHeader},
		{&m_sqpackDataSubHeaders[datIndex], sizeof m_sqpackDataSubHeaders[datIndex]},
		}) {
		if (relativeOffset < cb) {
			const auto src = std::span(static_cast<const char*>(ptr), cb)
				.subspan(static_cast<size_t>(relativeOffset));
			const auto available = std::min(out.size_bytes(), src.size_bytes());
			std::copy_n(src.begin(), available, out.begin());
			out = out.subspan(available);
			relativeOffset = 0;
		} else
			relativeOffset -= cb;

		if (out.empty())
			return length - out.size_bytes();
	}

	auto it = std::lower_bound(m_entries.begin(), m_entries.end(), nullptr, [&](const std::unique_ptr<Entry>& l, const std::unique_ptr<Entry>& r) {
		const auto ldfi = l ? l->DataFileIndex : datIndex;
		const auto rdfi = r ? r->DataFileIndex : datIndex;
		if (ldfi == rdfi) {
			const auto lo = l ? l->OffsetAfterHeaders : relativeOffset;
			const auto ro = r ? r->OffsetAfterHeaders : relativeOffset;
			return lo < ro;
		} else
			return ldfi < rdfi;
	});
	if (it != m_entries.begin())
		--it;
	if (it != m_entries.end()) {
		relativeOffset -= it->get()->OffsetAfterHeaders;
		if (relativeOffset >= INT32_MAX)
			__debugbreak();

		for (; it < m_entries.end(); ++it) {
			const auto& entry = *it->get();

			if (relativeOffset < entry.BlockSize) {
				const auto available = std::min(out.size_bytes(), static_cast<size_t>(entry.BlockSize - relativeOffset));
				const auto read = entry.Provider->Read(relativeOffset, out.data(), available);
				if (read != available) {
#ifdef _DEBUG
					if (IsDebuggerPresent()) {
						__debugbreak();
						entry.Provider->Read(relativeOffset, out.data(), available);
					}
#endif
					throw std::runtime_error("VirtualSqPack::ReadData: read != available");
				}

				out = out.subspan(read);
				relativeOffset = 0;

				if (out.empty())
					break;
			} else
				relativeOffset -= entry.BlockSize;

			if (relativeOffset < entry.PadSize) {
				const auto available = std::min(out.size_bytes(), static_cast<size_t>(entry.PadSize - relativeOffset));
				std::fill_n(out.begin(), available, 0);
				out = out.subspan(available);
				relativeOffset = 0;

				if (out.empty())
					break;
			} else
				relativeOffset -= entry.PadSize;
		}
	}

	return length - out.size_bytes();
}

uint64_t XivAlex::SqexDef::VirtualSqPack::SizeIndex1() const {
	return 0ULL +
		m_sqpackIndexHeader.HeaderSize +
		m_sqpackIndexSubHeader.HeaderSize +
		m_sqpackIndexSubHeader.FileSegment.Size +
		m_sqpackIndexSubHeader.DataFilesSegment.Size +
		m_sqpackIndexSubHeader.UnknownSegment3.Size +
		m_sqpackIndexSubHeader.FolderSegment.Size;
}

uint64_t XivAlex::SqexDef::VirtualSqPack::SizeIndex2() const {
	return 0ULL +
		m_sqpackIndex2Header.HeaderSize +
		m_sqpackIndex2SubHeader.HeaderSize +
		m_sqpackIndex2SubHeader.FileSegment.Size +
		m_sqpackIndex2SubHeader.DataFilesSegment.Size +
		m_sqpackIndex2SubHeader.UnknownSegment3.Size +
		m_sqpackIndex2SubHeader.FolderSegment.Size;
}

uint64_t XivAlex::SqexDef::VirtualSqPack::SizeData(uint32_t datIndex) const {
	if (datIndex >= m_sqpackDataSubHeaders.size())
		return 0;

	return 0ULL +
		m_sqpackDataHeader.HeaderSize +
		m_sqpackDataSubHeaders[datIndex].HeaderSize +
		m_sqpackDataSubHeaders[datIndex].DataSize;
}

class XivAlex::SqexDef::VirtualSqPack::Implementation {

};
