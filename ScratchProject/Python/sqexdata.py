import contextlib
import csv
import ctypes
import io
import os
import re
import shutil
import struct
import typing
import zlib

import PIL.Image
import PIL.ImageDraw

hash_table = (
    (0x00000000, 0x77073096, 0xEE0E612C, 0x990951BA, 0x076DC419, 0x706AF48F, 0xE963A535, 0x9E6495A3, 0x0EDB8832,
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
     0xB40BBE37, 0xC30C8EA1, 0x5A05DF1B, 0x2D02EF8D),
    (0x00000000, 0x191B3141, 0x32366282, 0x2B2D53C3, 0x646CC504, 0x7D77F445, 0x565AA786, 0x4F4196C7, 0xC8D98A08,
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
     0xB809AEB1, 0xA1129FF0, 0x8A3FCC33, 0x9324FD72),
    (0x00000000, 0x01C26A37, 0x0384D46E, 0x0246BE59, 0x0709A8DC, 0x06CBC2EB, 0x048D7CB2, 0x054F1685, 0x0E1351B8,
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
     0xBCDE8AB4, 0xBD1CE083, 0xBF5A5EDA, 0xBE9834ED),
    (0x00000000, 0xB8BC6765, 0xAA09C88B, 0x12B5AFEE, 0x8F629757, 0x37DEF032, 0x256B5FDC, 0x9DD738B9, 0xC5B428EF,
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
     0xCCB0A91F, 0x740CCE7A, 0x66B96194, 0xDE0506F1),
)

lang_prefix = {
    0: "",
    1: "_ja",
    2: "_en",
    3: "_de",
    4: "_fr",
    5: "_chs",
    6: "_cht",
    7: "_ko",
}


def sqexhash(data: str) -> int:
    data = data.lower().encode()
    result = 0xFFFFFFFF

    i = 0
    while i < len(data) // 4 * 4:
        result ^= int.from_bytes(data[i:i + 4], "little", signed=False)
        result = (hash_table[3][result & 0xFF] ^
                  hash_table[2][(result >> 8) & 0xFF] ^
                  hash_table[1][(result >> 16) & 0xFF] ^
                  hash_table[0][(result >> 24) & 0xFF])
        i += 4

    for i in range(i, len(data)):
        result = hash_table[0][(result ^ data[i]) & 0xFF] ^ (result >> 8)

    return result


class SqpackHeader(ctypes.LittleEndianStructure):
    _fields_ = (
        ("signature", ctypes.c_char * 12),  # SqPack\0\0\0\0\0\0
        ("header_length", ctypes.c_uint32),
        ("unknown_2", ctypes.c_uint32),
        ("sqpack_type", ctypes.c_uint32),  # 0: sqdb, 1: data, 2: index
        ("unknown", ctypes.c_uint8 * (0x3c0 - 0x018)),
        ("sha1", ctypes.c_uint8 * 20),
    )


class DataHeader(ctypes.LittleEndianStructure):
    _fields_ = (
        ("header_length", ctypes.c_uint32),
        ("null_1", ctypes.c_uint32),
        ("unknown_1", ctypes.c_uint32),  # 0x10
        ("data_size", ctypes.c_uint32),  # from end of this header to eof; divided by 0x08
        ("span_index", ctypes.c_uint32),  # 0x01 = .dat0, 0x02 = .dat1, 0x03 = .dat2, ...
        ("null_2", ctypes.c_uint32),
        ("max_file_size", ctypes.c_uint32),  # always 0x77359400 or 2GB
        ("null_3", ctypes.c_uint32),
        ("sha1_data", ctypes.c_uint8 * 20),  # from end of this header to eof
        ("unknown", ctypes.c_uint8 * (0x3c0 - 0x034)),
        ("sha1_header", ctypes.c_uint8 * 20),
    )


class BlockHeader(ctypes.LittleEndianStructure):
    COMPRESSED_SIZE_NOT_COMPRESSED = 32000

    _fields_ = (
        ("header_length", ctypes.c_uint32),
        ("version", ctypes.c_uint32),
        ("compressed_size", ctypes.c_uint32),
        ("decompressed_size", ctypes.c_uint32),
    )

    header_length: int
    version: int
    compressed_size: int
    decompressed_size: int

    data: typing.Optional[bytes] = None

    def is_compressed(self):
        return self.compressed_size != BlockHeader.COMPRESSED_SIZE_NOT_COMPRESSED and self.decompressed_size != 1

    @classmethod
    def from_fp(cls, segment: typing.Optional['FileSegmentItem'], offset: typing.Optional[int],
                fp: typing.Union[typing.BinaryIO, io.RawIOBase]):
        if segment is not None and offset is not None:
            fp.seek(segment.dat_offset + segment.data_entry_header.header_length + offset)
        self = cls()
        fp.readinto(self)
        if self.is_compressed():
            self.data = fp.read(self.compressed_size)
            self.data = zlib.decompress(self.data, -zlib.MAX_WBITS)
            assert self.decompressed_size == len(self.data)
        else:
            self.data = fp.read(self.decompressed_size)
        return self


class DataEntryHeader(ctypes.LittleEndianStructure):
    _fields_ = (
        ("header_length", ctypes.c_uint32),
        ("content_type", ctypes.c_uint32),  # 0x01: empty placeholder, 0x02: binary, 0x03: model, 0x04: texture
        ("decompressed_size", ctypes.c_uint32),
        ("unknown_1", ctypes.c_uint32),
        ("block_buffer_size", ctypes.c_uint32),
        ("num_blocks", ctypes.c_uint32),
    )

    header_length: int
    content_type: int
    decompressed_size: int
    unknown_1: int
    block_buffer_size: int
    num_blocks: int


class DataBlock(ctypes.LittleEndianStructure):
    _fields_ = (
        ("offset", ctypes.c_uint32),
        ("block_size", ctypes.c_int16),
        ("decompressed_data_size", ctypes.c_int16),
    )

    offset: int
    block_size: int
    decompressed_data_size: int


class Type4BlockTable(ctypes.LittleEndianStructure):
    _fields_ = (
        ("frame_offset", ctypes.c_uint32),
        ("frame_size", ctypes.c_uint32),
        ("decompressed_size", ctypes.c_uint32),
        ("block_table_offset", ctypes.c_uint32),
        ("num_sub_blocks", ctypes.c_uint32),
    )
    frame_offset: int
    frame_size: int
    decompressed_size: int
    block_table_offset: int
    num_sub_blocks: int

    sub_blocks: typing.Optional[DataBlock] = None


class ModelFileSqPackData(ctypes.LittleEndianStructure):
    _fields_ = (
        # chunk decompressed sizes
        ("stack_memory_size", ctypes.c_uint32),
        ("runtime_memory_size", ctypes.c_uint32),
        ("vertex_buffer_size", ctypes.c_uint32 * 3),
        ("edge_geometry_vertex_buffer_size", ctypes.c_uint32 * 3),
        ("index_buffer_size", ctypes.c_uint32 * 3),

        # chunk sizes
        ("compressed_stack_memory_size", ctypes.c_uint32),
        ("compressed_runtime_memory_size", ctypes.c_uint32),
        ("compressed_vertex_buffer_size", ctypes.c_uint32 * 3),
        ("compressed_edge_geometry_vertex_buffer_size", ctypes.c_uint32 * 3),
        ("compressed_index_buffer_size", ctypes.c_uint32 * 3),

        # chunk offsets
        ("stack_memory_offset", ctypes.c_uint32),
        ("runtime_memory_offset", ctypes.c_uint32),
        ("vertex_buffer_offset", ctypes.c_uint32 * 3),
        ("edge_geometry_vertex_buffer_offset", ctypes.c_uint32 * 3),
        ("index_buffer_offset", ctypes.c_uint32 * 3),

        # chunk start block index
        ("stack_data_block_index", ctypes.c_uint16),
        ("runtime_data_block_index", ctypes.c_uint16),
        ("vertex_data_block_index", ctypes.c_uint16 * 3),
        ("edge_geometry_vertex_data_block_index", ctypes.c_uint16 * 3),
        ("index_buffer_data_block_index", ctypes.c_uint16 * 3),

        # chunk num blocks
        ("stack_data_block_num", ctypes.c_uint16),
        ("runtime_data_block_num", ctypes.c_uint16),
        ("vertex_data_block_num", ctypes.c_uint16 * 3),
        ("edge_geometry_vertex_data_block_num", ctypes.c_uint16 * 3),
        ("index_buffer_data_block_num", ctypes.c_uint16 * 3),

        ("vertex_declaration_num", ctypes.c_uint16),
        ("material_num", ctypes.c_uint16),

        ("lod_num", ctypes.c_uint8),
        ("enable_index_buffer_streaming", ctypes.c_uint8),
        ("enable_edge_geometry", ctypes.c_uint8),
        ("padding1", ctypes.c_uint8),
    )

    stack_memory_size: int
    runtime_memory_size: int
    vertex_buffer_size: typing.List[int]
    edge_geometry_vertex_buffer_size: typing.List[int]
    index_buffer_size: typing.List[int]
    compressed_stack_memory_size: int
    compressed_runtime_memory_size: int
    compressed_vertex_buffer_size: typing.List[int]
    compressed_edge_geometry_vertex_buffer_size: typing.List[int]
    compressed_index_buffer_size: typing.List[int]
    stack_memory_offset: int
    runtime_memory_offset: int
    vertex_buffer_offset: typing.List[int]
    edge_geometry_vertex_buffer_offset: typing.List[int]
    index_buffer_offset: typing.List[int]
    stack_data_block_index: int
    runtime_data_block_index: int
    vertex_data_block_index: typing.List[int]
    edge_geometry_vertex_data_block_index: typing.List[int]
    index_buffer_data_block_index: typing.List[int]
    stack_data_block_num: int
    runtime_data_block_num: int
    vertex_data_block_num: typing.List[int]
    edge_geometry_vertex_data_block_num: typing.List[int]
    index_buffer_data_block_num: typing.List[int]
    vertex_declaration_num: int
    material_num: int
    lod_num: int
    enable_index_buffer_streaming: int
    enable_edge_geometry: int
    padding1: int


class ModelFileHeader(ctypes.LittleEndianStructure):
    _fields_ = (
        ("version", ctypes.c_uint32),
        ("stack_memory_size", ctypes.c_uint32),
        ("runtime_memory_size", ctypes.c_uint32),
        ("vertex_declaration_num", ctypes.c_uint16),
        ("material_num", ctypes.c_uint16),

        ("vertex_data_offset", ctypes.c_uint32 * 3),
        ("index_data_offset", ctypes.c_uint32 * 3),
        ("vertex_buffer_size", ctypes.c_uint32 * 3),
        ("index_buffer_size", ctypes.c_uint32 * 3),

        ("lod_num", ctypes.c_uint8),
        ("enable_index_buffer_streaming", ctypes.c_uint8),
        ("enable_edge_geometry", ctypes.c_uint8),
        ("padding1", ctypes.c_uint8),
    )

    version: int
    stack_memory_size: int
    runtime_memory_size: int
    vertex_declaration_num: int
    material_num: int

    vertex_data_offset: typing.List[int]
    index_data_offset: typing.List[int]
    vertex_buffer_size: typing.List[int]
    index_buffer_size: typing.List[int]

    lod_num: int
    enable_index_buffer_streaming: int
    enable_edge_geometry: int
    padding1: int


class SegmentHeader(ctypes.LittleEndianStructure):
    class SegmentHeaderItem(ctypes.LittleEndianStructure):
        _fields_ = (
            ("num", ctypes.c_uint32),
            ("offset", ctypes.c_uint32),
            ("size", ctypes.c_uint32),
            ("sha1", ctypes.c_uint8 * 20),
        )

    _fields_ = (
        ("header_length", ctypes.c_uint32),
        ("segment1", SegmentHeaderItem),
        ("padding1", ctypes.c_uint8 * 0x2C),
        ("segment2", SegmentHeaderItem),
        ("padding2", ctypes.c_uint8 * 0x28),
        ("segment3", SegmentHeaderItem),
        ("padding3", ctypes.c_uint8 * 0x28),
        ("segment4", SegmentHeaderItem),
        ("padding4", ctypes.c_uint8 * 0x28),
    )


class FileSegmentItem(ctypes.LittleEndianStructure):
    _fields_ = (
        ("name_hash", ctypes.c_uint32),
        ("path_hash", ctypes.c_uint32),
        ("offset", ctypes.c_uint32),  # multiply by 0x08
        ("padding", ctypes.c_uint32),
    )

    name_hash: int
    path_hash: int
    offset: int
    padding: int

    data_entry_header: DataEntryHeader

    @property
    def dat_index(self):
        return (self.offset & 0x0F) // 2

    @dat_index.setter
    def dat_index(self, v: int):
        self.offset = (self.offset & ~0x0F) | (v * 2)

    @property
    def dat_offset(self):
        return (self.offset & 0xFFFFFFF0) * 8

    @dat_offset.setter
    def dat_offset(self, v):
        if v & 0b111:
            raise ValueError
        v = v // 8
        if v & 0x0F:
            raise ValueError
        self.offset = (self.offset & 0x0F) | v

    def read_data(self, fp: io.RawIOBase) -> bytes:
        if self.data_entry_header.content_type == 1:
            return b""
        elif self.data_entry_header.content_type == 2:
            return self.read_data_binary(fp)
        elif self.data_entry_header.content_type == 3:
            return self.read_data_model(fp)
        elif self.data_entry_header.content_type == 4:
            return self.read_data_texture(fp)
        else:
            raise ValueError

    def read_data_binary(self, fp: io.RawIOBase) -> bytes:
        block_tables = [DataBlock() for _ in range(self.data_entry_header.num_blocks)]

        fp.seek(self.dat_offset + ctypes.sizeof(DataEntryHeader))
        for block_table in block_tables:
            fp.readinto(block_table)

        fp_file = io.BytesIO()
        for block_table in block_tables:
            fp_file.write(BlockHeader.from_fp(self, block_table.offset, fp).data)

        return fp_file.getvalue()

    def pack_data_binary(self, data: bytes):
        self.data_entry_header = DataEntryHeader()
        self.data_entry_header.header_length = ctypes.sizeof(self)
        self.data_entry_header.content_type = 2
        self.data_entry_header.decompressed_size = len(data)
        self.data_entry_header.unknown_1 = 0  # TODO
        self.data_entry_header.block_buffer_size = 32767
        blocks = [self.data_entry_header]
        for i in range(0, len(data), 30000):
            chunk = data[i:i + 30000]
            block_header = BlockHeader()
            block_header.header_length = ctypes.sizeof(block_header)
            block_header.version = 0
            block_header.decompressed_size = len(data)
            block_header.compressed_size = BlockHeader.COMPRESSED_SIZE_NOT_COMPRESSED
            blocks.append(block_header)
            if len(chunk) > 1:
                c = zlib.compressobj(level=zlib.Z_BEST_COMPRESSION, wbits=-zlib.MAX_WBITS)
                compressed = c.compress(chunk) + c.flush()
                if len(compressed) < len(chunk) - 32:
                    block_header.compressed_size = len(compressed)
                    blocks.append(compressed)
                else:
                    blocks.append(chunk)
            else:
                blocks.append(chunk)

        fp_file = io.BytesIO()
        for block in blocks:
            fp_file.write(block)

        return fp_file.getvalue()

    def read_data_texture(self, fp: io.RawIOBase) -> bytes:
        block_tables = [Type4BlockTable() for _ in range(self.data_entry_header.num_blocks)]

        fp.seek(self.dat_offset + ctypes.sizeof(DataEntryHeader))
        for block_table in block_tables:
            fp.readinto(block_table)

        for block_table in block_tables:
            if not block_table.frame_size:
                block_table.sub_blocks = []
                continue

            sub_block_sizes = list(struct.unpack(
                "<" + "h" * block_table.num_sub_blocks,
                fp.read(2 * block_table.num_sub_blocks)))
            block_table.sub_blocks = [DataBlock(
                offset=block_table.frame_offset,
                block_size=sub_block_sizes[0],
                decompressed_data_size=-1,
            )]
            for j in range(1, len(sub_block_sizes)):
                block_table.sub_blocks.append(DataBlock(
                    offset=(block_table.sub_blocks[-1].offset +
                            block_table.sub_blocks[-1].block_size),
                    block_size=sub_block_sizes[j],
                    decompressed_data_size=-1,
                ))

        extra_header_size = block_tables[0].frame_offset
        if extra_header_size < ctypes.sizeof(TexHeader):
            raise ValueError(f"Invalid extra header size ({extra_header_size} < {ctypes.sizeof(TexHeader)})")

        fp.seek(self.dat_offset + self.data_entry_header.header_length)
        extra_header = fp.read(extra_header_size)
        tex_header = TexHeader.from_buffer_copy(extra_header)

        if tex_header.num_mipmaps > len(block_tables):
            raise ValueError(f"num_mipmaps({tex_header.num_mipmaps}) > len(block_tables)({len(block_tables)})")

        if ctypes.sizeof(TexHeader) + tex_header.num_mipmaps * 4 > extra_header_size:
            raise ValueError(f"Invalid extra header size ({extra_header_size} != "
                             f"{ctypes.sizeof(TexHeader)} + {tex_header.num_mipmaps} * 4)")

        tex_header.mipmap_offsets = list(struct.unpack(
            "<" + ("I" * tex_header.num_mipmaps),
            extra_header[ctypes.sizeof(tex_header):][:4 * tex_header.num_mipmaps]))
        if len(tex_header.mipmap_offsets) < len(block_tables):
            tex_header.mipmap_offsets += [0] * (len(block_tables) - len(tex_header.mipmap_offsets))

        fp_file = io.BytesIO()
        fp_file.write(extra_header)
        for j, (mipmap_offset, block_table) in enumerate(zip(tex_header.mipmap_offsets, block_tables)):
            if mipmap_offset:
                fp_file.seek(mipmap_offset)
            for sub_block in block_table.sub_blocks:
                block_header = BlockHeader()
                offset = self.dat_offset + self.data_entry_header.header_length + sub_block.offset
                fp.seek(offset)
                fp.readinto(block_header)
                if block_header.is_compressed():
                    data = fp.read(block_header.compressed_size)
                    data = zlib.decompress(data, -zlib.MAX_WBITS)
                    assert len(data) == block_header.decompressed_size
                else:
                    data = fp.read(block_header.decompressed_size)
                fp_file.write(data)
        return fp_file.getvalue()

    def read_data_model(self, fp: io.RawIOBase) -> bytes:
        fp_file = io.BytesIO()
        header = ModelFileSqPackData()

        fp.seek(self.dat_offset + ctypes.sizeof(DataEntryHeader))
        fp.readinto(header)

        num_blocks = header.index_buffer_data_block_index[2] + header.index_buffer_data_block_num[2]
        block_sizes = list(struct.unpack(
            "<" + ("H" * num_blocks),
            fp.read(2 * num_blocks)
        ))
        block_offsets = [0]
        for i in block_sizes[:-1]:
            block_offsets.append(block_offsets[-1] + i)

        model_header = ModelFileHeader()
        model_header.version = self.data_entry_header.num_blocks
        model_header.vertex_declaration_num = header.vertex_declaration_num
        model_header.material_num = header.material_num
        model_header.lod_num = header.lod_num
        model_header.enable_index_buffer_streaming = header.enable_index_buffer_streaming
        model_header.enable_edge_geometry = header.enable_edge_geometry

        assert (ctypes.sizeof(model_header) == 0x44)
        fp_file.seek(ctypes.sizeof(model_header))
        block_index = 0
        assert block_offsets[0] == header.stack_memory_offset
        assert block_index == header.stack_data_block_index
        comps = 0
        for _ in range(header.stack_data_block_num):
            bhdr = BlockHeader.from_fp(self, block_offsets[block_index], fp)
            comps += bhdr.compressed_size
            fp_file.write(bhdr.data)
            model_header.stack_memory_size += len(bhdr.data)
            block_index += 1
        assert (model_header.stack_memory_size + 127) // 128 * 128 == header.stack_memory_size
        assert sum(block_sizes[header.stack_data_block_index + i] for i in
                   range(header.stack_data_block_num)) == header.compressed_stack_memory_size

        assert block_offsets[block_index] == header.runtime_memory_offset
        assert block_index == header.runtime_data_block_index
        comps = 0
        for _ in range(header.runtime_data_block_num):
            bhdr = BlockHeader.from_fp(self, block_offsets[block_index], fp)
            comps += bhdr.compressed_size
            fp_file.write(bhdr.data)
            model_header.runtime_memory_size += len(bhdr.data)
            block_index += 1
        assert (model_header.runtime_memory_size + 127) // 128 * 128 == header.runtime_memory_size
        assert sum(block_sizes[header.runtime_data_block_index + i] for i in
                   range(header.runtime_data_block_num)) == header.compressed_runtime_memory_size

        for i in range(3):
            if header.vertex_data_block_num[i] != 0:
                assert header.vertex_buffer_offset[i] == block_offsets[block_index]
                assert header.vertex_data_block_index[i] == block_index
                model_header.vertex_data_offset[i] = fp_file.tell()

                comps = 0
                for _ in range(header.vertex_data_block_num[i]):
                    bhdr = BlockHeader.from_fp(self, block_offsets[block_index], fp)
                    comps += bhdr.compressed_size
                    fp_file.write(bhdr.data)
                    model_header.vertex_buffer_size[i] += len(bhdr.data)
                    block_index += 1
                assert (model_header.vertex_buffer_size[i] + 127) // 128 * 128 == header.vertex_buffer_size[i]
                assert sum(block_sizes[header.vertex_data_block_index[i] + j] for j in
                           range(header.vertex_data_block_num[i])) == header.compressed_vertex_buffer_size[i]

            if header.edge_geometry_vertex_data_block_num[i] != 0:
                assert header.edge_geometry_vertex_buffer_offset[i] == block_offsets[block_index]
                assert header.edge_geometry_vertex_data_block_index[i] == block_index
                evbs = 0
                comps = 0
                for _ in range(header.edge_geometry_vertex_data_block_num[i]):
                    bhdr = BlockHeader.from_fp(self, block_offsets[block_index], fp)
                    comps += bhdr.compressed_size
                    fp_file.write(bhdr.data)
                    evbs += len(bhdr.data)
                    block_index += 1
                assert (evbs + 127) // 128 * 128 == header.edge_geometry_vertex_buffer_size[i]
                assert sum(block_sizes[header.edge_geometry_vertex_data_block_index[i] + j] for j in
                           range(header.edge_geometry_vertex_data_block_num[i])) == \
                       header.compressed_edge_geometry_vertex_buffer_size[i]

            if header.index_buffer_data_block_num[i] != 0:
                assert header.index_buffer_offset[i] == block_offsets[block_index]
                assert header.index_buffer_data_block_index[i] == block_index
                model_header.index_data_offset[i] = fp_file.tell()

                for _ in range(header.index_buffer_data_block_num[i]):
                    bhdr = BlockHeader.from_fp(self, block_offsets[block_index], fp)
                    comps += bhdr.compressed_size
                    fp_file.write(bhdr.data)
                    model_header.index_buffer_size[i] += len(bhdr.data)
                    block_index += 1
                assert (model_header.index_buffer_size[i] + 127) // 128 * 128 == header.index_buffer_size[i]
                assert sum(block_sizes[header.index_buffer_data_block_index[i] + j] for j in
                           range(header.index_buffer_data_block_num[i])) == header.compressed_index_buffer_size[i]

        assert self.data_entry_header.decompressed_size == fp_file.tell()
        fp_file.seek(0)
        fp_file.write(model_header)
        return fp_file.getvalue()


class FolderSegmentItem(ctypes.LittleEndianStructure):
    _fields_ = (
        ("name_hash", ctypes.c_uint32),
        ("file_offset", ctypes.c_uint32),
        ("file_segment_size", ctypes.c_uint32),
        ("padding", ctypes.c_uint32),
    )

    files: typing.List[FileSegmentItem]


class ExhColumnDefinition(ctypes.BigEndianStructure):
    _fields_ = (
        ("type", ctypes.c_uint16),
        ("offset", ctypes.c_uint16),
    )


class ExhPageDefinition(ctypes.BigEndianStructure):
    _fields_ = (
        ("start_id", ctypes.c_uint32),
        ("row_count", ctypes.c_uint32),
    )


class ExhHeader(ctypes.BigEndianStructure):
    _fields_ = (
        ("signature", ctypes.c_uint32),
        ("unknown_1", ctypes.c_uint16),
        ("data_offset", ctypes.c_uint16),
        ("num_col", ctypes.c_uint16),
        ("num_page", ctypes.c_uint16),
        ("num_lang", ctypes.c_uint16),
        ("unknown_2", ctypes.c_uint16),
        ("unknown_3", ctypes.c_uint8),
        ("variant", ctypes.c_uint8),
        ("unknown_4", ctypes.c_uint16),
        ("num_entries", ctypes.c_uint32),
        ("unknown_5", ctypes.c_uint32),
        ("unknown_6", ctypes.c_uint32),
    )

    columns: typing.List[ExhColumnDefinition]
    pages: typing.List[ExhPageDefinition]
    languages: typing.List[int]


class TexHeader(ctypes.LittleEndianStructure):
    _fields_ = (
        ("unknown_1", ctypes.c_uint16),
        ("header_size", ctypes.c_uint16),
        ("compression_type", ctypes.c_uint32),
        ("decompressed_width", ctypes.c_uint16),
        ("decompressed_height", ctypes.c_uint16),
        ("depth", ctypes.c_uint16),
        ("num_mipmaps", ctypes.c_uint16),
        ("unknown_2", ctypes.c_uint8 * 0x0b),
    )

    unknown_1: int
    header_size: int
    compression_type: int
    decompressed_width: int
    decompressed_height: int
    depth: int
    num_mipmaps: int
    unknown_2: typing.List[int]
    mipmap_offsets: typing.Optional[typing.List[int]] = None


class FdtHeader(ctypes.LittleEndianStructure):
    _fields_ = (
        ("signature", ctypes.c_uint32),
        ("version", ctypes.c_uint32),
        ("glyph_header_offset", ctypes.c_uint32),
        ("knhd_header_offset", ctypes.c_uint32),
        ("null_1", ctypes.c_uint8 * 0x10),
    )

    SIGNATURE: typing.ClassVar[int] = 0x76736366
    VERSION: typing.ClassVar[int] = 0x30303130

    signature: int
    version: int
    glyph_header_offset: int
    knhd_header_offset: int
    null_1: typing.List[int]

    fthd_header: typing.Optional['FdtFontTableHeader']
    knhd_header: typing.Optional['FdtKerningHeader']


class FdtFontTableHeader(ctypes.LittleEndianStructure):
    _fields_ = (
        ("signature", ctypes.c_uint32),
        ("glyph_count", ctypes.c_uint32),
        ("kerning_entry_count", ctypes.c_int32),
        ("null_1", ctypes.c_uint32),
        ("image_width", ctypes.c_uint16),
        ("image_height", ctypes.c_uint16),
        ("size", ctypes.c_float),
        ("font_height", ctypes.c_uint32),
        ("font_ascent", ctypes.c_uint32),
    )

    SIGNATURE: typing.ClassVar[int] = 0x64687466

    signature: int
    glyph_count: int
    kerning_entry_count: int
    null_1: int
    image_width: int
    image_height: int
    size: float
    font_height: int
    font_ascent: int

    glyphs: typing.Optional[typing.List['FdtGlyphEntry']] = None

    def glyph_map(self) -> typing.Dict[str, 'FdtGlyphEntry']:
        return {
            x.char: x for x in self.glyphs
        }


def sqex_str_from_int(val: int, encoding: str = "utf-8") -> str:
    return struct.pack(">I", val).decode(encoding).rsplit("\0", 1)[-1] or "\0"


def sqex_int_from_str(new_char: str) -> typing.Tuple[int, int]:
    if len(new_char) != 1:
        raise ValueError
    u8, sjis = struct.unpack(">IH",
                             f"\0\0\0{new_char}".encode("utf-8")[-4:] +
                             f"\0{new_char}".encode("shift_jis", errors="replace")[-2:])
    return u8, sjis


class FdtGlyphEntry(ctypes.LittleEndianStructure):
    _fields_ = (
        ("char_utf8", ctypes.c_uint32),
        ("char_sjis", ctypes.c_uint16),
        ("image_index", ctypes.c_uint16),
        ("x", ctypes.c_uint16),
        ("y", ctypes.c_uint16),
        ("width", ctypes.c_uint8),
        ("height", ctypes.c_uint8),
        ("offset_x", ctypes.c_int8),
        ("offset_y", ctypes.c_int8),
    )

    char_utf8: int
    char_sjis: int
    image_index: int
    x: int
    y: int
    width: int
    height: int
    offset_x: int
    offset_y: int

    @property
    def char(self) -> str:
        return sqex_str_from_int(self.char_utf8)

    @char.setter
    def char(self, new_char: str):
        self.char_utf8, self.char_sjis = sqex_int_from_str(new_char)


class FdtKerningHeader(ctypes.LittleEndianStructure):
    _fields_ = (
        ("signature", ctypes.c_uint32),
        ("count", ctypes.c_uint32),
        ("null_1", ctypes.c_uint64)
    )

    SIGNATURE: typing.ClassVar[int] = 0x64686e6b

    signature: int
    count: int
    null_1: int

    entries: typing.Optional[typing.List['FdtKerningEntry']] = None


class FdtKerningEntry(ctypes.LittleEndianStructure):
    _fields_ = (
        ("char1_utf8", ctypes.c_uint32),
        ("char2_utf8", ctypes.c_uint32),
        ("char1_sjis", ctypes.c_uint16),
        ("char2_sjis", ctypes.c_uint16),
        ("offset_x", ctypes.c_int32),
    )

    char1_utf8: int
    char2_utf8: int
    char1_sjis: int
    char2_sjis: int
    offset_x: int

    @property
    def char1(self) -> str:
        return sqex_str_from_int(self.char1_utf8)

    @char1.setter
    def char1(self, new_char1: str):
        self.char1_utf8, self.char1_sjis = sqex_int_from_str(new_char1)

    @property
    def char2(self) -> str:
        return sqex_str_from_int(self.char2_utf8)

    @char2.setter
    def char2(self, new_char2: str):
        self.char2_utf8, self.char2_sjis = sqex_int_from_str(new_char2)


class ImageDecoding:
    @staticmethod
    def bgra(data: bytes, target_width: int, target_height: int):
        return PIL.Image.frombytes("RGBA", (target_width, target_height), data, "raw", "BGRA")

    @staticmethod
    def rgba4444(data: bytes, target_width: int, target_height: int):
        return PIL.Image.frombytes("RGBA", (target_width, target_height), data, "raw", "RGBA;4B")


class ImageEncoding:
    @staticmethod
    def rgba4444(img: PIL.Image.Image):
        data = img.tobytes("raw", "RGBA")
        res = bytearray(len(data) // 2)
        for b1, b2, i in zip(data[::2], data[1::2], range(len(res))):
            res[i] = (b1 >> 4) | (b2 & 0xF0)
        return bytes(res)

    @staticmethod
    def bgra(img: PIL.Image.Image):
        return img.tobytes("raw", "BGRA")


def parse_fdt(path: str):
    fp: io.RawIOBase
    with open(path, "rb") as fp:
        fp.readinto(fdt_header := FdtHeader())
        if fdt_header.signature != FdtHeader.SIGNATURE:
            raise ValueError

        fp.seek(fdt_header.glyph_header_offset)
        fdt_header.fthd_header = FdtFontTableHeader()
        fp.readinto(fdt_header.fthd_header)
        if fdt_header.fthd_header.signature != FdtFontTableHeader.SIGNATURE:
            raise ValueError
        fdt_header.fthd_header.glyphs = [FdtGlyphEntry() for _ in range(fdt_header.fthd_header.glyph_count)]
        for glyph in fdt_header.fthd_header.glyphs:
            fp.readinto(glyph)

        fp.seek(fdt_header.knhd_header_offset)
        fdt_header.knhd_header = FdtKerningHeader()
        fp.readinto(fdt_header.knhd_header)
        if fdt_header.knhd_header.signature != FdtKerningHeader.SIGNATURE:
            raise ValueError
        fdt_header.knhd_header.entries = [FdtKerningEntry() for _ in range(fdt_header.knhd_header.count)]
        for entry in fdt_header.knhd_header.entries:
            fp.readinto(entry)
    return fdt_header


def write_fdt(fp: typing.Union[typing.BinaryIO, io.RawIOBase], fdt_header: FdtHeader):
    fdt_header.signature = FdtHeader.SIGNATURE
    fdt_header.version = FdtHeader.VERSION
    fdt_header.glyph_header_offset = ctypes.sizeof(FdtHeader)
    fdt_header.knhd_header_offset = (ctypes.sizeof(FdtHeader)
                                     + ctypes.sizeof(FdtFontTableHeader)
                                     + ctypes.sizeof(FdtGlyphEntry) * len(fdt_header.fthd_header.glyphs))
    for i in range(0x10):
        fdt_header.null_1[i] = 0

    fdt_header.fthd_header.signature = FdtFontTableHeader.SIGNATURE
    fdt_header.fthd_header.glyph_count = len(fdt_header.fthd_header.glyphs)
    fdt_header.fthd_header.kerning_entry_count = 0  # TODO
    fdt_header.fthd_header.null_1 = 0

    fdt_header.knhd_header.signature = FdtKerningHeader.SIGNATURE
    fdt_header.knhd_header.count = len(fdt_header.knhd_header.entries)
    fdt_header.knhd_header.null_1 = 0

    fp.write(fdt_header)
    fp.write(fdt_header.fthd_header)
    for glyph in fdt_header.fthd_header.glyphs:
        glyph.simple_char = ord('A')  # TODO
        glyph.simple_char_class = 0  # TODO
        fp.write(glyph)
    fp.write(fdt_header.knhd_header)
    for kerning in fdt_header.knhd_header.entries:
        fp.write(kerning)


def parse_exh(data: bytes):
    exh_header = ExhHeader()
    ctypes.memmove(ctypes.addressof(exh_header), data, ctypes.sizeof(exh_header))
    offset = ctypes.sizeof(exh_header)
    exh_header.columns = []
    exh_header.pages = []
    for i in range(exh_header.num_col):
        col = ExhColumnDefinition()
        ctypes.memmove(ctypes.addressof(col), data[offset:offset + ctypes.sizeof(col)], ctypes.sizeof(col))
        offset += ctypes.sizeof(ExhColumnDefinition)
        exh_header.columns.append(col)
    for i in range(exh_header.num_page):
        page = ExhPageDefinition()
        ctypes.memmove(ctypes.addressof(page), data[offset:offset + ctypes.sizeof(page)], ctypes.sizeof(page))
        offset += ctypes.sizeof(ExhPageDefinition)
        exh_header.pages.append(page)
    exh_header.languages = struct.unpack("<" + "H" * exh_header.num_lang,
                                         data[offset:offset + 2 * exh_header.num_lang])
    return exh_header


def extract(sqpack: str, xiv_file: str, target_dir: str,
            paths: typing.Optional[typing.List[typing.Tuple[int, str]]] = None):
    if paths:
        paths = set(sqexhash(x) if isinstance(x, str) else x for x in paths)
    # https://github.com/goaaats/ffxiv-explorer-fork/blob/develop/src/main/java/com/fragmenterworks/ffxivextract/models/SqPack_DatFile.java
    fp_index: io.RawIOBase
    fp_datas = []
    shutil.rmtree(target_dir, ignore_errors=True)
    print("Working on", xiv_file)
    with contextlib.ExitStack() as exit_stack, \
            open(f"{sqpack}\\{xiv_file}.win32.index", "rb") as fp_index:
        for i in range(0, 100):
            try:
                # noinspection PyTypeChecker
                fp_data: io.RawIOBase = open(f"{sqpack}\\{xiv_file}.win32.dat{i}", "rb")
                fp_datas.append(fp_data)
                # noinspection PyTypeChecker
                exit_stack.enter_context(fp_datas[i])
            except FileNotFoundError:
                break

        sqh = SqpackHeader()
        fp_index.readinto(sqh)

        sh = SegmentHeader()
        fp_index.seek(sqh.header_length)
        fp_index.readinto(sh)

        files = {}
        fp_index.seek(sh.segment1.offset)
        for i in range(0, sh.segment1.size, ctypes.sizeof(FileSegmentItem)):
            item = FileSegmentItem()
            fp_index.readinto(item)
            files[sh.segment1.offset + i] = item

            if paths and item.path_hash not in paths:
                continue

            item.data_entry_header = DataEntryHeader()
            fp_data = fp_datas[item.dat_index]
            fp_data.seek(item.dat_offset)
            fp_data.readinto(item.data_entry_header)
            os.makedirs(os.path.join(target_dir, f"~{item.path_hash:08x}"), exist_ok=True)
            current_file = os.path.join(target_dir,
                                        f"~{item.path_hash:08x}",
                                        f"~{item.name_hash:08x}")
            # if item.name_hash == 286132449: breakpoint()
            try:
                data = item.read_data(fp_data)
            except ValueError:
                breakpoint()
                continue
            with open(current_file, "wb+") as fp_file:
                fp_file.write(data)

    print()
    print("Loading name data...")

    names_db = {
        "exd",
        "root.exl",
        "AXIS_36.fdt",
        "KrnAXIS_360.fdt",
        *(f"font{x}.tex" for x in range(100))
    }
    for i in ("MiedingerMid", "TrumpGothic", "KrnAXIS", "Meidinger", "Jupiter", "AXIS"):
        for j in (21.6, 14.4, 40.0, 9.6, 16.8, 36.0, 90.0, 68.0, 46.0):
            names_db.add(f"{i}_{int(j)}.fdt")
            names_db.add(f"{i}_{int(j)}_lobby.fdt")
            names_db.add(f"{i}_{int(j * 10)}.fdt")
            names_db.add(f"{i}_{int(j * 10)}_lobby.fdt")

    for root, dirs, files in os.walk("sqexdata_namedict"):
        for file in files:
            with open(os.path.join(root, file), "r") as fp:
                fp: io.TextIOBase
                if file.endswith(".log"):
                    reobj = re.compile(r"HashTracker\s+[0-9a-f]+: ([^,\s]+)(?:,([^,\s]+))?.*=>\s*[0-9a-f]+\s*$")
                    for i, line in enumerate(fp):
                        s = reobj.search(line)
                        if not s:
                            continue
                        name = "".join(x for x in [s.group(1), s.group(2)] if x is not None)
                        names_db.update(name.rsplit("/", 1))
                        names_db.update(name.replace("_lobby", "").rsplit("/", 1))
                elif file.endswith(".txt"):
                    for line in fp:
                        names_db.update(x for x in line.strip().rsplit("/", 1) if x)

    try:
        with open(os.path.join(target_dir, f"~{sqexhash('exd'):08x}", f"~{sqexhash('root.exl'):08x}")) as index:
            index.readline()
            for line in index:
                path, name = f"exd/{line.split(',')[0]}.exh".rsplit("/", 1)
                names_db.update((path, name))

                with open(os.path.join(target_dir, f"~{sqexhash(path):08x}", f"~{sqexhash(name):08x}"), "rb") as fp:
                    fp: io.RawIOBase
                    exh_header = parse_exh(fp.read())

                for page in exh_header.pages:
                    for lang_code in exh_header.languages:
                        names_db.update(f"exd/{line.split(',')[0]}_{page.start_id}{lang_prefix[lang_code]}.exd"
                                        .rsplit("/", 1))
    except FileNotFoundError:
        pass

    for x in list(names_db):
        for lngname in list(lang_prefix.values())[1:]:
            for postfix in "_.":
                if lngname + postfix in x:
                    a, b = x.split(lngname + postfix, 1)
                    b = postfix + b
                    break
            else:
                continue
            break
        else:
            continue
        for lngname in lang_prefix.values():
            names_db.add(f"{a}{lngname}{b}")
    names_db = {f"~{sqexhash(x):08x}": x for x in names_db}

    print("Renaming...")
    for dirname in os.listdir(target_dir):
        for filename in os.listdir(os.path.join(target_dir, dirname)):
            if filename in names_db:
                src = os.path.join(target_dir, dirname, filename)
                dst = os.path.join(target_dir, dirname, names_db[filename])
                # print(f"FILE: {src} => {dst}")
                shutil.move(src, dst)

                with open(dst, "rb") as fp:
                    fp: io.RawIOBase
                    fp.readinto(tex_header := TexHeader())
                    if tex_header.compression_type not in (0x1450, 0x1451, 0x1440):
                        continue
                    tex_header.mipmap_offsets = list(struct.unpack(
                        "<" + ("I" * tex_header.num_mipmaps),
                        fp.read(4 * tex_header.num_mipmaps)))
                    fp.seek(0)
                    data = fp.read()

                for i, offset in enumerate(tex_header.mipmap_offsets):
                    whole_data = data[offset:]
                    if tex_header.compression_type in (0x1451,):  # ARGB
                        img = ImageDecoding.bgra(whole_data,
                                                 int(tex_header.decompressed_width // pow(2, i)),
                                                 int(tex_header.decompressed_height // pow(2, i)))
                        with open(dst + f".{i}.png", "wb") as fp:
                            img.save(fp, "png")
                    elif tex_header.compression_type in (0x1440,):  # ARGB4444
                        img = ImageDecoding.rgba4444(whole_data,
                                                     int(tex_header.decompressed_width // pow(2, i)),
                                                     int(tex_header.decompressed_height // pow(2, i)))
                        r, g, b, a = img.split()
                        for j, img_split in enumerate((b, g, r, a)):
                            with open(dst + f".{i}.{j}.png", "wb") as fp:
                                img_split.save(fp, "png")

    for dirname in os.listdir(target_dir):
        if dirname in names_db:
            src = os.path.join(target_dir, dirname)
            dst = os.path.join(target_dir, names_db[dirname])
            # print(f"DIR: {src} => {dst}")
            shutil.move(os.path.realpath(src), os.path.realpath(dst))

    return 0


def merge_exd(target_file, target_original_file, source_file, target_lang, source_lang):
    try:
        with open(target_original_file, "rb") as fp:
            tgt = parse_exh(fp.read())
    except FileNotFoundError as e:
        print(e)
        return

    os.makedirs(os.path.dirname(target_file), exist_ok=True)
    for page in tgt.pages:
        for lang_code in tgt.languages:
            shutil.copy(f"{target_original_file[:-4]}_{page.start_id}{lang_prefix[lang_code]}.exd",
                        f"{target_file[:-4]}_{page.start_id}{lang_prefix[lang_code]}.exd")
    shutil.copy(target_original_file, target_file)

    try:
        with open(source_file, "rb") as fp:
            src = parse_exh(fp.read())
    except FileNotFoundError as e:
        print(e)
        return
    if target_lang not in tgt.languages or source_lang not in src.languages:
        return

    for page in tgt.pages:
        shutil.copy(f"{source_file[:-4]}_{page.start_id}{lang_prefix[source_lang]}.exd",
                    f"{target_file[:-4]}_{page.start_id}{lang_prefix[target_lang]}.exd")


def dump_fdts(fns: typing.List[str]):
    fp = io.StringIO(newline="")
    fpcsv = csv.writer(fp)
    fpcsv.writerow([
        "Name",
        "FDT.signature",
        "FDT.version",
        "FDT.glyph_header_offset",
        "FDT.knhd_header_offset",
        "FDT.null_1",
        "FTHD.signature",
        "FTHD.glyph_count",
        "FTHD.kerning_entry_count",
        "FTHD.null_1",
        "FTHD.image_width",
        "FTHD.image_height",
        "FTHD.size",
        "FTHD.font_height",
        "FTHD.font_ascent",
        "KNHD.signature",
        "KNHD.count",
        "KNHD.null_1",
    ])
    for fn in fns:
        fdt = parse_fdt(fn)
        fpcsv.writerow([
            os.path.basename(fn),
            f"0x{fdt.signature:08x}",
            f"0x{fdt.version:x}",
            f"0x{fdt.glyph_header_offset:x}",
            f"0x{fdt.knhd_header_offset:x}",
            f"{' '.join(f'{x:02x}' for x in fdt.null_1)}",
            f"0x{fdt.fthd_header.signature:08x}",
            f"{fdt.fthd_header.glyph_count}",
            f"{fdt.fthd_header.kerning_entry_count}",
            f"{fdt.fthd_header.null_1}",
            f"{fdt.fthd_header.image_width}",
            f"{fdt.fthd_header.image_height}",
            f"{fdt.fthd_header.size}",
            f"{fdt.fthd_header.font_height}",
            f"{fdt.fthd_header.font_ascent}",
            f"0x{fdt.knhd_header.signature:08x}",
            f"{fdt.knhd_header.count}",
            f"{fdt.knhd_header.null_1}",
        ])
    return fp.getvalue()


def dump_fdt(fn: str):
    fdt = parse_fdt(fn)
    with open(f"{fn}.info.txt", "w") as fp:
        fp.write(f"FDT.signature=0x{fdt.signature:08x}\n"
                 f"FDT.version=0x{fdt.version:x}\n"
                 f"FDT.glyph_header_offset=0x{fdt.glyph_header_offset:x}\n"
                 f"FDT.knhd_header_offset=0x{fdt.knhd_header_offset:x}\n"
                 f"FDT.null_1={' '.join(f'{x:02x}' for x in fdt.null_1)}\n"
                 "\n"
                 f"FTHD.signature=0x{fdt.fthd_header.signature:08x}\n"
                 f"FTHD.glyph_count={fdt.fthd_header.glyph_count}\n"
                 f"FTHD.kerning_entry_count={fdt.fthd_header.kerning_entry_count}\n"
                 f"FTHD.null_1={fdt.fthd_header.null_1}\n"
                 f"FTHD.image_width={fdt.fthd_header.image_width}\n"
                 f"FTHD.image_height={fdt.fthd_header.image_height}\n"
                 f"FTHD.size={fdt.fthd_header.size}\n"
                 f"FTHD.font_height={fdt.fthd_header.font_height}\n"
                 f"FTHD.font_ascent={fdt.fthd_header.font_ascent}\n"
                 "\n"
                 f"KNHD.signature=0x{fdt.knhd_header.signature:08x}\n"
                 f"KNHD.count={fdt.knhd_header.count}\n"
                 f"KNHD.null_1={fdt.knhd_header.null_1}\n")

    if fdt.fthd_header.glyphs:
        with open(f"{fn}.glyph.csv", "w", newline="", encoding="utf-8") as fp:
            fp.write("\ufeff")
            writer = csv.writer(fp)
            writer.writerow(("char", "image_index", "x", "y", "width", "height", "offset_x", "offset_y"))
            for glyph in fdt.fthd_header.glyphs:
                writer.writerow((
                    glyph.char,
                    glyph.image_index,
                    glyph.x, glyph.y,
                    glyph.width, glyph.height,
                    glyph.offset_x, glyph.offset_y,
                ))

    if fdt.knhd_header.entries:
        with open(f"{fn}.knhd.csv", "w", newline="", encoding="utf-8") as fp:
            fp.write("\ufeff")
            writer = csv.writer(fp)
            writer.writerow(("char1", "char2", "offset_x"))
            for entry in fdt.knhd_header.entries:
                writer.writerow((
                    entry.char1, entry.char2, entry.offset_x,
                ))


def __main__():
    return extract(r"Z:\scratch\t2", r"000000", r"t\000000")
    # return extract(r"C:\Program Files (x86)\SquareEnix\FINAL FANTASY XIV - A Realm Reborn\game\sqpack\ffxiv",
    #                r"040000",
    #                r"g\ffxiv\040000",
    #                paths=["chara/equipment/e0100",
    #                       "chara/equipment/e0100/material/v0001",
    #                       "chara/equipment/e0100/material/v0002",
    #                       "chara/equipment/e0100/model",
    #                       "chara/equipment/e0100/texture"])
    # return extract(r"C:\Program Files (x86)\SquareEnix\FINAL FANTASY XIV - A Realm Reborn\game\sqpack\ffxiv", r"000000", r"g\ffxiv\000000")
    # return extract(r"D:\scratch", r"000000", r"t\000000")
    # return extract(r"C:\Program Files (x86)\FINAL FANTASY XIV - KOREA\game\sqpack", r"ffxiv\000000", r"k\ffxiv\000000")
    #
    # axis96 = parse_fdt(r"Z:\scratch\k\ffxiv\000000\common\font\AXIS_96.fdt")
    # axis96_glyph_map = axis96.fthd_header.glyph_map()
    # os.makedirs("testres", exist_ok=True)
    # for rpfx, basedir in {
    #     # "k": r"Z:\scratch\g\ffxiv\000000\common\font",
    #     "g": r"Z:\scratch\k\ffxiv\000000\common\font",
    # }.items():
    #     d: typing.Dict[str, FdtHeader] = {}
    #     for fn in os.listdir(basedir):
    #         ffn = os.path.join(basedir, fn)
    #         try:
    #             d[fn] = parse_fdt(ffn)
    #         except ValueError:
    #             continue
    #         with open(os.path.join("testres", fn), "wb") as fp:
    #             for i, glyph in enumerate(d[fn].fthd_header.glyphs):
    #                 if 'A' <= glyph.char.upper() <= 'Z':
    #                     d[fn].fthd_header.glyphs[i] = axis96_glyph_map[glyph.char]
    #             write_fdt(fp, d[fn])
    # return
    #
    # for rpfx, basedir in {
    #     # "k": r"Z:\scratch\k\ffxiv\000000\common\font",
    #     "g": r"Z:\scratch\g\ffxiv\000000\common\font",
    # }.items():
    #     fns = []
    #     for fn in os.listdir(basedir):
    #         ffn = os.path.join(basedir, fn)
    #         try:
    #             dump_fdt(ffn)
    #             fns.append(ffn)
    #         except Exception as e:
    #             print(ffn, e)
    #     with open("summary.csv", "w") as fp:
    #         fp.write(dump_fdts(fns))
    # return

    for rpfx, basedir in {
        "g": r"Z:\scratch\g\ffxiv\000000\common\font",
        # "k": r"Z:\scratch\k\ffxiv\000000\common\font",
    }.items():
        d: typing.Dict[str, FdtHeader] = {}
        for fn in os.listdir(basedir):
            if fn.startswith("~"):
                continue
            ffn = os.path.join(basedir, fn)
            try:
                d[fn] = parse_fdt(ffn)
            except ValueError:
                continue

        texs: typing.Dict[str, typing.List[typing.Tuple[str, PIL.Image.Image]]] = {
            "": [],
            "_lobby": [],
            "_krn_": [],
        }
        for k, v in texs.items():
            try:
                for i in range(1, 8):
                    for j in range(4):
                        fn = f"font{k}{i}.tex.0.{j}.png"
                        with open(os.path.join(basedir, fn), "rb") as fp:
                            im = PIL.Image.open(fp)
                            im.load()
                            v.append((fn, im))
            except FileNotFoundError:
                pass

        print(texs)
        for fn, f in sorted(d.items()):
            if fn != "AXIS_18.fdt":
                continue
            f: FdtHeader
            fthd = f.fthd_header
            print(f"{fn:<28}",
                  f"s={fthd.size:6.3f} s1={fthd.font_height:3} s2={fthd.font_ascent:3} u2={fthd.kerning_entry_count}")

            # for entry in f.knhd_header.entries:
            #     print(entry.char1, entry.char2, chr(entry.unknown_3), chr(entry.unknown_4),
            #           entry.unknown_5, entry.unknown_6, entry.unknown_7, entry.unknown_8)

            if fn.startswith("Krn"):
                ctex = texs["_krn_"]
            else:
                ctex = texs[""]

            glyph_map: typing.Dict[str, FdtGlyphEntry] = {x.char: x for x in fthd.glyphs}
            kern_map: typing.Dict[str, FdtKerningEntry] = {f"{x.char1}{x.char2}": x for x in
                                                           f.knhd_header.entries}
            txt = (f"{fn}\n\n"
                   "Uppercase: ABCDEFGHIJKLMNOPQRSTUVWXYZ\n"
                   "Lowercase: abcdefghijklmnopqrstuvwxyz\n"
                   "Numbers: 0123456789 ０１２３４５６７８９\n"
                   "SymbolsH: `~!@#$%^&*()_+-=[]{}\\|;':\",./<>?\n"
                   "SymbolsF: ｀～！＠＃＄％＾＆＊（）＿＋－＝［］｛｝￦｜；＇：＂，．／＜＞？\n"
                   "Hiragana: あかさたなはまやらわ\n"
                   "KatakanaH: ｱｶｻﾀﾅﾊﾏﾔﾗﾜ\n"
                   "KatakanaF: アカサタナハマヤラワ\n"
                   "Hangul: 가나다라마바사ㅇㅈㅊㅋㅌㅍㅎ\n"
                   "\n"
                   "<<SupportedUnicode>>\n"
                   "π™′＾¿¿‰øØ×∞∩£¥¢Ð€ªº†‡¤ ŒœŠšŸÅωψ↑↓→←⇔⇒♂♀♪¶§±＜＞≥≤≡÷½¼¾©®ª¹²³\n"
                   "※⇔｢｣«»≪≫《》【】℉℃‡。·••‥…¨°º‰╲╳╱☁☀☃♭♯✓〃¹²³\n"
                   "●◎○■□▲△▼▽∇♥♡★☆◆◇♦♦♣♠♤♧¶αß∇ΘΦΩδ∂∃∀∈∋∑√∝∞∠∟∥∪∩∨∧∫∮∬\n"
                   "∴∵∽≒≠≦≤≥≧⊂⊃⊆⊇⊥⊿⌒─━│┃│¦┗┓└┏┐┌┘┛├┝┠┣┤┥┫┬┯┰┳┴┷┸┻╋┿╂┼￢￣，－．／：；＜＝＞［＼］＿｀｛｜｝～＠\n"
                   "⑴⑵⑶⑷⑸⑹⑺⑻⑼⑽⑾⑿⒀⒁⒂⒃⒄⒅⒆⒇⓪①②③④⑤⑥⑦⑧⑨⑩⑪⑫⑬⑭⑮⑯⑰⑱⑲⑳\n"
                   "₀₁₂₃₄₅₆₇₈₉№ⅠⅡⅢⅣⅤⅥⅦⅧⅨⅩⅰⅱⅲⅳⅴⅵⅶⅷⅸⅹ０１２３４５６７８９！？＂＃＄％＆＇（）＊＋￠￤￥\n"
                   "ＡＢＣＤＥＦＧＨＩＪＫＬＭＮＯＰＱＲＳＴＵＶＷＸＹＺａｂｃｄｅｆｇｈｉｊｋｌｍｎｏｐｑｒｓｔｕｖｗｘｙｚ\n"
                   "\n"
                   "<<GameSpecific>>\n"
                   " \n"
                   "\n"
                   "\n"
                   "\n"
                   "\n"
                   "<<Kerning>>\n"
                   "AC AG AT AV AW AY LT LV LW LY TA Ta Tc Td Te Tg To VA Va Vc Vd Ve Vg Vm Vo Vp Vq Vu\n"
                   "A\0C A\0G A\0T A\0V A\0W A\0Y L\0T L\0V L\0W L\0Y T\0A T\0a T\0c T\0d T\0e T\0g T\0o V\0A V\0a V\0c V\0d V\0e V\0g V\0m V\0o V\0p V\0q V\0u\n"
                   "WA We Wq YA Ya Yc Yd Ye Yg Ym Yn Yo Yp Yq Yr Yu eT oT\n"
                   "W\0A W\0e W\0q Y\0A Y\0a Y\0c Y\0d Y\0e Y\0g Y\0m Y\0n Y\0o Y\0p Y\0q Y\0r Y\0u e\0T o\0T\n"
                   "Az Fv Fw Fy TV TW TY Tv Tw Ty VT WT YT tv tw ty vt wt yt\n"
                   "A\0z F\0v F\0w F\0y T\0V T\0W T\0Y T\0v T\0w T\0y V\0T W\0T Y\0T t\0v t\0w t\0y v\0t w\0t y\0t\n")
            x = 0
            line_height = fthd.font_height + 1
            h = line_height
            last_offset_x = 0
            w = 0
            for lt, t in zip("\0" + txt[:-1], txt):
                if t == "\n":
                    h += line_height
                    w = max(w, x - last_offset_x)
                    x = 0
                    continue
                if t == '\0':
                    continue
                if t not in glyph_map:
                    t = '=' if "=" in glyph_map else "!"
                glyph = glyph_map[t]
                kerning = kern_map.get(f"{lt}{t}", None)

                if kerning is not None:
                    offset_x = kerning.offset_x + kerning.offset_x
                else:
                    offset_x = glyph.offset_x
                x += glyph.width + offset_x
                last_offset_x = offset_x

            w = max(w, x - last_offset_x)

            img = PIL.Image.new("RGBA", (w, h), (0, 0, 0, 255))
            x = y = 0
            for lt, t in zip("\0" + txt[:-1], txt):
                if t == "\n":
                    y += line_height
                    x = 0
                    continue
                if t == '\0':
                    continue
                if t not in glyph_map:
                    t = '=' if "=" in glyph_map else "!"
                glyph = glyph_map[t]
                kerning = kern_map.get(f"{lt}{t}", None)

                if kerning is not None:
                    offset_x_curr = kerning.offset_x
                    offset_x = glyph.offset_x
                else:
                    offset_x_curr = 0
                    offset_x = glyph.offset_x

                print(x, offset_x_curr, offset_x, y, glyph.offset_y)
                alpha = ctex[glyph.image_index][1].crop(
                    (glyph.x, glyph.y, glyph.x + glyph.width, glyph.y + glyph.height))
                color = PIL.Image.new("RGB", alpha.size, (0, 255, 255))
                img.paste(
                    color,
                    (x + offset_x_curr, y + glyph.offset_y),
                    alpha
                )
                x += glyph.width + offset_x_curr + offset_x
                pass
            with open(f"test_{rpfx}_{fn}.png", "wb") as fp:
                img.save(fp, "png")

            pass

    return
    extract(
        r"C:\Program Files (x86)\SquareEnix\FINAL FANTASY XIV - A Realm Reborn\game\sqpack",
        r"ffxiv\0a0000",
        r"g\ffxiv\0a0000",
    )
    extract(
        r"C:\Program Files (x86)\SquareEnix\FINAL FANTASY XIV - A Realm Reborn\game\sqpack",
        r"ex3\030300",
        r"g\ex3\030300",
    )
    extract(
        r"C:\Program Files (x86)\FINAL FANTASY XIV - KOREA\game\sqpack",
        r"ffxiv\0a0000",
        r"k\ffxiv\0a0000",
    )
    extract(
        r"C:\Program Files (x86)\FINAL FANTASY XIV - KOREA\game\sqpack",
        r"ex3\030300",
        r"k\ex3\030300",
    )
    exhs_g = [os.path.join(x[0][15:], y)
              for x in os.walk(r"g\ffxiv\0a0000")
              for y in x[2]
              if y.endswith(".exh")]
    exhs_ko = [os.path.join(x[0][15:], y)
               for x in os.walk(r"k\ffxiv\0a0000")
               for y in x[2]
               if y.endswith(".exh")]
    exhs = set(exhs_g + exhs_ko)
    for f in exhs:
        merge_exd(f"r\\ffxiv\\0a0000\\{f}", f"k\\ffxiv\\0a0000\\{f}", f"g\\ffxiv\\0a0000\\{f}", 7, 2)


if __name__ == "__main__":
    exit(__main__())
