#include <iostream>
#include <Windows.h>
#include <windowsx.h>

#include "XivRes/FontdataStream.h"
#include "XivRes/GameReader.h"
#include "XivRes/MipmapStream.h"
#include "XivRes/PackedFileUnpackingStream.h"
#include "XivRes/PixelFormats.h"
#include "XivRes/TextureStream.h"
#include "XivRes/FontGenerator/CodepointLimitingFixedSizeFont.h"
#include "XivRes/FontGenerator/FontdataPacker.h"
#include "XivRes/FontGenerator/GameFontdataFixedSizeFont.h"
#include "XivRes/FontGenerator/MergedFixedSizeFont.h"
#include "XivRes/FontGenerator/TextMeasurer.h"
#include "XivRes/Internal/TexturePreview.Windows.h"

static const auto* const pszTestString = (
	u8"Uppercase: ABCDEFGHIJKLMNOPQRSTUVWXYZ\n"
	u8"Lowercase: abcdefghijklmnopqrstuvwxyz\n"
	u8"Numbers: 0123456789 ０１２３４５６７８９\n"
	u8"SymbolsH: `~!@#$%^&*()_+-=[]{}\\|;':\",./<>?\n"
	u8"SymbolsF: ｀～！＠＃＄％＾＆＊（）＿＋－＝［］｛｝￦｜；＇：＂，．／＜＞？\n"
	u8"Hiragana: あかさたなはまやらわ\n"
	u8"KatakanaH: ｱｶｻﾀﾅﾊﾏﾔﾗﾜ\n"
	u8"KatakanaF: アカサタナハマヤラワ\n"
	u8"Hangul: 가나다라마바사아자차카타파하\n"
	u8"Hangul: ㄱㄴㄷㄹㅁㅂㅅㅇㅈㅊㅋㅌㅍㅎ\n"
	u8"Chinese: 天地玄黄，宇宙洪荒。\n"
	u8"\n"
	u8"<<SupportedUnicode>>\n"
	u8"π™′＾¿¿‰øØ×∞∩£¥¢Ð€ªº†‡¤ ŒœŠšŸÅωψ↑↓→←⇔⇒♂♀♪¶§±＜＞≥≤≡÷½¼¾©®ª¹²³\n"
	u8"※⇔｢｣«»≪≫《》【】℉℃‡。·••‥…¨°º‰╲╳╱☁☀☃♭♯✓〃¹²³\n"
	u8"●◎○■□▲△▼▽∇♥♡★☆◆◇♦♦♣♠♤♧¶αß∇ΘΦΩδ∂∃∀∈∋∑√∝∞∠∟∥∪∩∨∧∫∮∬\n"
	u8"∴∵∽≒≠≦≤≥≧⊂⊃⊆⊇⊥⊿⌒─━│┃│¦┗┓└┏┐┌┘┛├┝┠┣┤┥┫┬┯┰┳┴┷┸┻╋┿╂┼￢￣，－．／：；＜＝＞［＼］＿｀｛｜｝～＠\n"
	u8"⑴⑵⑶⑷⑸⑹⑺⑻⑼⑽⑾⑿⒀⒁⒂⒃⒄⒅⒆⒇⓪①②③④⑤⑥⑦⑧⑨⑩⑪⑫⑬⑭⑮⑯⑰⑱⑲⑳\n"
	u8"₀₁₂₃₄₅₆₇₈₉№ⅠⅡⅢⅣⅤⅥⅦⅧⅨⅩⅰⅱⅲⅳⅴⅵⅶⅷⅸⅹ０１２３４５６７８９！？＂＃＄％＆＇（）＊＋￠￤￥\n"
	u8"ＡＢＣＤＥＦＧＨＩＪＫＬＭＮＯＰＱＲＳＴＵＶＷＸＹＺａｂｃｄｅｆｇｈｉｊｋｌｍｎｏｐｑｒｓｔｕｖｗｘｙｚ\n"
	u8"\n"
	u8"<<GameSpecific>>\n"
	u8" \n"
	u8"\n"
	u8"\n"
	u8"\n"
	u8"\n"
	u8"<<Kerning>>\n"
	u8"AC AG AT AV AW AY LT LV LW LY TA Ta Tc Td Te Tg To VA Va Vc Vd Ve Vg Vm Vo Vp Vq Vu\n"
	u8"A\u200cC A\u200cG A\u200cT A\u200cV A\u200cW A\u200cY L\u200cT L\u200cV L\u200cW L\u200cY T\u200cA T\u200ca T\u200cc T\u200cd T\u200ce T\u200cg T\u200co V\u200cA V\u200ca V\u200cc V\u200cd V\u200ce V\u200cg V\u200cm V\u200co V\u200cp V\u200cq V\u200cu\n"
	u8"WA We Wq YA Ya Yc Yd Ye Yg Ym Yn Yo Yp Yq Yr Yu eT oT\n"
	u8"W\u200cA W\u200ce W\u200cq Y\u200cA Y\u200ca Y\u200cc Y\u200cd Y\u200ce Y\u200cg Y\u200cm Y\u200cn Y\u200co Y\u200cp Y\u200cq Y\u200cr Y\u200cu e\u200cT o\u200cT\n"
	u8"Az Fv Fw Fy TV TW TY Tv Tw Ty VT WT YT tv tw ty vt wt yt\n"
	u8"A\u200cz F\u200cv F\u200cw F\u200cy T\u200cV T\u200cW T\u200cY T\u200cv T\u200cw T\u200cy V\u200cT W\u200cT Y\u200cT t\u200cv t\u200cw t\u200cy v\u200ct w\u200ct y\u200ct\n"
	u8"\n"
	u8"(테스트) (테스트test) (test테스트) (ㅌㅅㅌ) (test)\n"
	u8"(测验测) (测验测test) (test测验测)\n"
	u8"게임을 종료하시겠습니까? “elemental”"
	u8"\n"
	u8"https://generator.lorem-ipsum.info/\n"
	u8"\n"
	u8"選界加身続供丁囲雅密支更人害個坂聞。内長撃藤対謙聞本需同製暮児業線滅囲野調。望財禁夜以会調死供第提形直禁仁女温食申。島族新著止転立量経文北場。\n"
	u8"復多記他黒転全覧泉画意捜楽件否掲必共保員。本肥痛金細近家体口選断戦。及係国約個境国表春受止持試有。属私院家決声掲暖岡不演閔得単行学上。方役写平去習転員沸速予立江。\n"
	u8"核続墜覚真球億質応給付半月特級後備今。残使読文鹿竹激度川社品勇堂相田給。政露虎東本分加供自初可哲。陸材球鮮首学強際読訂分所様功供雄。絶単長事抜真高相社投役毎相屋盗階。\n"
	u8"企皇機監中断新省属品森自戦得不法聖。王図爆行世安考史前間中図車川誤暮産。共企記防氏更同金州分需葉禁民次在禎。転将春喫書調質権戦年火買百依審芸講。\n"
	u8"講十整事住央適点発紙禁裁気株討口第害区動。遊長閣判続飛望彩観趣用広。設町建載形職開制更逮育禁未洋刊富活著以徳。装融陸詳意戦月弟和銀治裁運付試朝片。\n"
	u8"注批盆国提先図佐生治優集賞。情買告原原将衛開断同術郎都。策会霊的現何被意型文止規員子。面地平詳幼投上裏同電大果盛週転通。種定観週半思銘験旅攻阜成誌暮。\n"
	u8"台的格責図決違下監戻潔単八会査唄東出労並。回需禁変開名来海重学東蒸。盛限城報政付次報供設逃竜朝向。条書歌画谷統法軽入査取週者経崎内禁。事促版限覧川断立毎石宇側。\n"
	u8"時表掲紙堀独伴馬彰神定爆質売属。住教栓霊録質北速国間舎開軍媛銭。能保第熊堀香健刊普覧因昧留乗芸無立番川。規社覧反月六考彫尾善議協禁回報動左責負時。\n"
	u8"会号最花行属肉詳公情社進報。窓注参音得夏額来女首攻開柏月品使難浮図公。木本梨議面題議修市後写汗門情資文鳥黒触況。音図切並聖率追更文丸州供。祖済情雪文木能子書最溝方安保申。\n"
	u8"国本詳表国医林禁意発普月北射芸予。回城議健不作作日差難国区供融拠立犯美好気。満則革続書転止阜見題中配表。家好死就局久然見勝聞組化表電夜積登立道条。\n"
	u8"校豊画離話普辞場苦目式壽家第。要富末川済担書延売更脱置覧典掲工真作。稿以合天教変覧立飛強庭伏。教関日転念転岡則画起書質明見崎感。庫統文本面滞聞著定路来真拉専効。\n"
	u8"温痛口制事億生康行採社器米病年子訪面元処。最更社乗府真蔵段体校職供地唯間動校役安鈴。当応作経出訂破置面表拘騰絶政逮。向増用聞徴地矢善惑挙面護用参促多害加食銭。\n"
	u8"玄局講投排田能帯章暑島会要立周向扱覧。東課書号応質権求竹中官地給案燈来高視香度。笑村図企台防衆弾売機神購物子訪。図太融社焼覧重研劇際一今装派感力防引。\n"
	u8"調持賀因改論駐稔見予津鎌嶋発放歴授津回供。母禁男選点健無公初復政表街土社県石画。立覧技見役健姿海裏談作定。象政英夢貢張宮応山中野校氏銀千勢来施。\n"
	u8"他者人地属刷店業藤投治間鳥穫福今映。年済切内挑教惑志監激本掲就阻本年。統抜岡店速呼性寿軍住子務必副賛。目載携校歌経服知最室月苦掲徐囲長委募橋重。\n"
	u8"通星逃記開省船測軟看現理損。脅儀囲二広天内和転支車局輸趣然理権育試。目情廟要担変取徳詳編規善。事岡謙蘭預署由暮示社滅悲福属悪請。球疑年側公欲問息清応放護涯表供野。\n"
	u8"者呼球求力部無年連入有旅禁軽。道形有日数明読子参分以究問禁明取子泉鉄。地徹供田著継蔵断火場経月力確加理築。更事巻開画植隠消謹子化南善補却挿事代現。\n"
	u8"聞米用万主存無速便各治競。熱禁料完楽異蕉濃高人月手済写欧作載告眠才。治明多良師広阜週渡面項州野法。第著日週回港政教観村民競動浦。住室月校権子連異時調含医学点健。\n"
	u8"気検好景判訪臓健技写提品。汐男仙著地泉退何百受日案識年京適約議禽世。思高型稿移動識身然困花慎風未太新。望最文小掲甲国院信線過選周有。観注出査級使動約表産古経神選庭性首。\n"
	u8"憶転受直窃常分登情苦自回認。円作京高投任年呼賞海式文済特飛十厳。惑準鳥理園北喜模会広地古聞治同裁大。答藤覇野索右有国毎路過常変両思球。"
	u8"\n"
	u8"언론·출판·집회·결사의 자유. 교육의 자주성·전문성·정치적 중립성 및 대학의 자율성은 법률이 정하는 바에 의하여 보장된다, 체포 또는 구속을 당한 자의 가족등 법률이 정하는\n"
	u8"자에게는 그 이유와 일시·장소가 지체없이 통지되어야 한다. 대통령에 대한 탄핵소추는 국회재적의원 과반수의 발의와 국회재적의원 3분의 2 이상의 찬성이 있어야 한다.\n"
	u8"국교는 인정되지 아니하며. 사법절차가 준용되어야 한다, 이 경우 그 명령에 의하여 개정 또는 폐지되었던 법률은 그 명령이 승인을 얻지 못한 때부터 당연히 효력을 회복한다.\n"
	u8"계엄을 선포한 때에는 대통령은 지체없이 국회에 통고하여야 한다. 국회의원과 정부는 법률안을 제출할 수 있다, 선전포고와 강화를 한다, 다만, 모든 국민은 인간다운 생활을 할 권리를 가진다.\n"
	u8"헌법재판소에서 법률의 위헌결정. 내부규율과 사무처리에 관한 규칙을 제정할 수 있다. 재판의 전심절차로서 행정심판을 할 수 있다. 대한민국의 주권은 국민에게 있고.\n"
	u8"국민경제의 발전을 위한 중요정책의 수립에 관하여 대통령의 자문에 응하기 위하여 국민경제자문회의를 둘 수 있다. 감사원은 세입·세출의 결산을 매년 검사하여 대통령과\n"
	u8"차년도 국회에 그 결과를 보고하여야 한다. 모든 국민은 법률이 정하는 바에 의하여 납세의 의무를 진다, 1차에 한하여 중임할 수 있다.\n"
	u8"모든 국민은 직업선택의 자유를 가진다, 국가는 농·어민과 중소기업의 자조조직을 육성하여야 하며, 그 정치적 중립성은 준수된다. 학교교육 및 평생교육을 포함한 교육제도와 그 운영.\n"
	u8"사회적 특수계급의 제도는 인정되지 아니하며. 법률에 저촉되지 아니하는 범위안에서 내부규율에 관한 규칙을 제정할 수 있다. 헌법개정안은 국회가 의결한 후 30일 이내에 국민투표에 붙여\n"
	u8"국회의원선거권자 과반수의 투표와 투표자 과반수의 찬성을 얻어야 한다, 제2항의 재판관중 3인은 국회에서 선출하는 자를.\n"
	u8"국무회의의 구성원으로서 국정을 심의한다. 다만. 외국에 대하여 국가를 대표한다. 언론·출판에 대한 허가나 검열과 집회·결사에 대한 허가는 인정되지 아니한다.\n"
	u8"제1항의 지시를 받은 당해 행정기관은 이에 응하여야 한다, 대법원에 대법관을 둔다, 비상계엄이 선포된 때에는 법률이 정하는 바에 의하여 영장제도. \n"
	u8"정부는 회계연도마다 예산안을 편성하여 회계연도 개시 90일전까지 국회에 제출하고.\n"
	u8"감사위원은 원장의 제청으로 대통령이 임명하고. 국회는 헌법 또는 법률에 특별한 규정이 없는 한 재적의원 과반수의 출석과 출석의원 과반수의 찬성으로 의결한다.\n"
	u8"1차에 한하여 중임할 수 있다. 직전대통령이 없을 때에는 대통령이 지명한다.\n"
	u8"\n"
	u8"값 가價价価jià, jiè, ·jie\n"
	u8"껍질 각/내려칠 각, 구역질 하는 모殼壳殻qiào, ké\n"
	u8"깨달을 각, 깰 교覺觉覚jué, jiào\n"
	u8"근거 거據据拠jù, jū\n"
	u8"검사할 검檢检検jiǎn\n"
	u8"칼 검劍剑剣jiàn\n"
	u8"검소할 검儉俭倹jiǎn\n"
	u8"칠 격擊击撃jī\n"
	u8"지름길 경/길 경徑径径jìng\n"
	u8"지날 경/글 경經经経jīng, jìng\n"
	u8"줄기 경莖茎茎jīng\n"
	u8"가벼울 경輕轻軽qīng\n"
	u8"닭 계鷄鸡鶏jī\n"
	u8"이을 계繼继継jì\n"
	u8"관계할 관關关関guān\n"
	u8"너그러울 관寬宽寛kuān\n"
	u8"볼 관觀观観guān, guàn\n"
	u8"넓을 광廣广広guǎng\n"
	u8"무너질 괴, 앓을 회壞坏壊huài\n"
	u8"몰 구驅驱駆qū\n"
	u8"구라파 구/칠 구歐欧欧ōu\n"
	u8"때릴 구毆殴殴ōu\n"
	u8"예 구/옛 구舊旧旧jiù\n"
	u8"구분할 구/지경 구, 숨길 우區区区qū, ōu\n"
	u8"나라 국國国国guó\n"
	u8"권세 권權权権quán\n"
	u8"권할 권勸劝勧quàn\n"
	u8"거북 귀, 땅 이름 구, 터질 균龜龟亀\n"
	u8"돌아갈 귀歸归帰guī\n"
	u8"기운 기, 보낼 희氣气気qì\n"
	u8"푸를 녹(록)綠绿緑\n"
	u8"번뇌할 뇌惱恼悩nǎo\n"
	u8"골 뇌/뇌수 뇌腦脑脳nǎo\n"
	u8"끊을 단斷断断duàn\n"
	u8"둥글 단/경단 단團团団tuán\n"
	u8"멜 담擔担担dān, dǎn, dàn\n"
	u8"쓸개 담膽胆胆dǎn\n"
	u8"무리 당黨党党dǎng\n"
	u8"마땅 당當当当dāng, dàng\n"
	u8"대할 대對对対duì\n"
	u8"띠 대帶带帯dài\n"
	u8"그림 도圖图図tú\n"
	u8"홀로 독獨独独dú\n"
	u8"읽을 독, 구절 두讀读読dú, dòu\n"
	u8"등 등燈灯灯dēng\n"
	u8"어지러울 란(난)亂乱乱luàn\n"
	u8"볼 람(남)覽览覧lǎn\n"
	u8"올 래(내)來来来lái, ·lai\n"
	u8"두 량(양), 냥 냥(양)兩两両liǎng\n"
	u8"힘쓸 려(여)勵励励lì\n"
	u8"책력 력(역)曆历暦lì\n"
	u8"지날 력(역)/책력 력(역)歷历歴lì\n"
	u8"그리워할 련(연)/그릴 련(연)戀恋恋liàn, lián\n"
	u8"사냥 렵(엽), 개 이름 작獵猎猟liè\n"
	u8"신령 령(영)靈灵霊líng\n"
	u8"나이 령(영)齡龄齢líng\n"
	u8"예도 례(예)禮礼礼lǐ\n"
	u8"화로 로(노)爐炉炉lú\n"
	u8"일할 로(노)勞劳労láo\n"
	u8"푸를 록(녹)綠绿緑lǜ, lù\n"
	u8"기록할 록(녹), 사실할 려(여)錄录録lù\n"
	u8"비 올 롱(농), 여울 랑(낭), 물瀧泷滝lóng, shuāng\n"
	u8"여울 뢰(뇌)瀨濑瀬lài\n"
	u8"의뢰할 뢰(뇌)賴赖頼lài\n"
	u8"용 룡(용), 언덕 롱(농), 얼룩龍龙竜lóng\n"
	u8"다락 루(누)樓楼楼lóu\n"
	u8"보루 루(누), 끌밋할 뢰(뇌), 귀壘垒塁lěi\n"
	u8"찰 만滿满満mǎn\n"
	u8"물굽이 만, 물에 적셨다 말릴 탄灣湾湾wān\n"
	u8"오랑캐 만蠻蛮蛮mán\n"
	u8"팔 매賣卖売mài\n"
	u8"보리 맥麥麦麦mài\n"
	u8"미륵 미/두루 미彌弥弥mí\n"
	u8"터럭 발髮发髪fà, fā\n"
	u8"물 뿌릴 발潑泼溌pō\n"
	u8"필 발發发発fā\n"
	u8"술 괼 발醱酦醗\n"
	u8"변할 변變变変biàn\n"
	u8"가 변邊边辺biān\n"
	u8"떡 병餠饼餅\n"
	u8"보배 보寶宝宝bǎo\n"
	u8"베낄 사寫写写xiě\n"
	u8"실 사, 가는 실 멱絲丝糸sī\n"
	u8"말씀 사辭辞辞cí\n"
	u8"실마리 서緖绪緒\n"
	u8"풀 석釋释釈shì\n"
	u8"선 선禪禅禅chán, shàn\n"
	u8"말씀 설, 달랠 세, 기뻐할 열, 벗說说説shuō, shuì, yuè\n"
	u8"가늘 섬纖纤繊xiān, qiàn\n"
	u8"다스릴 섭/잡을 섭, 편안할 녑(엽)攝摄摂shè\n"
	u8"소리 성聲声声shēng\n"
	u8"해 세歲岁歳suì\n"
	u8"떠들 소騷骚騒sāo\n"
	u8"사를 소燒烧焼shāo\n"
	u8"무리 속, 이을 촉屬属属shǔ, zhǔ\n"
	u8"이을 속續续続xù\n"
	u8"뿌릴 쇄, 나눌 시, 끊어지지 않는灑洒洒sǎ\n"
	u8"따를 수, 게으를 타隨随随suí\n"
	u8"셈 수, 자주 삭, 촘촘할 촉數数数shù, shǔ, shuò\n"
	u8"짐승 수獸兽獣shòu\n"
	u8"목숨 수壽寿寿shòu\n"
	u8"엄숙할 숙肅肃粛sù\n"
	u8"젖을 습, 나라 이름 합, 물 이름濕湿湿shī\n"
	u8"노끈 승繩绳縄shéng\n"
	u8"열매 실實实実shí\n"
	u8"두 쌍/쌍 쌍雙双双shuāng\n"
	u8"아이 아, 다시 난 이 예兒儿児ér, ní\n"
	u8"버금 아, 누를 압亞亚亜yà\n"
	u8"악할 악, 미워할 오惡恶悪è, ě, wū, wù\n"
	u8"노래 악, 즐길 락(낙), 좋아할 요樂乐楽lè, yào, yuè\n"
	u8"누를 압, 싫어할 염壓压圧yā, yà\n"
	u8"앵두 앵櫻樱桜yīng\n"
	u8"약 약藥药薬yào\n"
	u8"모양 양, 상수리나무 상樣样様yàng\n"
	u8"사양할 양讓让譲ràng\n"
	u8"술 빚을 양釀酿醸niàng, niáng\n"
	u8"더불 여/줄 여與与与yǔ, yú, yù\n"
	u8"역 역驛驿駅yì\n"
	u8"번역할 역譯译訳yì\n"
	u8"인연 연緣缘縁yuán\n"
	u8"볼 열/셀 열閱阅閲yuè\n"
	u8"소금 염鹽盐塩yán\n"
	u8"영화 영/꽃 영榮荣栄róng\n"
	u8"강 이름 영潁颍頴yǐng\n"
	u8"경영할 영營营営yíng\n"
	u8"기릴 예/명예 예譽誉誉yù\n"
	u8"날카로울 예, 창 태銳锐鋭ruì\n"
	u8"편안할 온, 편안할 은穩稳穏wěn\n"
	u8"노래 요謠谣謡yáo\n"
	u8"하 위/할 위爲为為\n"
	u8"에워쌀 위, 나라 국圍围囲wéi\n"
	u8"거짓 위僞伪偽\n"
	u8"숨을 은隱隐隠yǐn\n"
	u8"마실 음飮饮飲\n"
	u8"응할 응應应応yīng\n"
	u8"의원 의醫医医yī\n"
	u8"두 이/갖은두 이貳贰弐èr\n"
	u8"사다리 잔, 성할 진棧栈桟zhàn\n"
	u8"잔인할 잔/남을 잔殘残残cán\n"
	u8"누에 잠, 지렁이 천蠶蚕蚕cán\n"
	u8"섞일 잡雜杂雑zá\n"
	u8"문서 장, 형상 상狀状状\n"
	u8"장수 장/장차 장將将将jiāng, jiàng\n"
	u8"오장 장臟脏臓zàng, zāng\n"
	u8"씩씩할 장/전장 장莊庄荘zhuāng\n"
	u8"장할 장壯壮壮zhuàng\n"
	u8"장려할 장奬奖奨jiǎng\n"
	u8"재계할 재/집 재, 상복 자齋斋斎zhāi\n"
	u8"오로지 전, 모일 단專专専zhuān\n"
	u8"싸움 전戰战戦zhàn\n"
	u8"구를 전轉转転zhuǎn, zhuàn\n"
	u8"돈 전錢钱銭qián\n"
	u8"전할 전傳传伝chuán, zhuàn\n"
	u8"훔칠 절竊窃窃qiè\n"
	u8"고요할 정靜静静jìng\n"
	u8"가지런할 제, 재계할 재, 옷자락 자齊齐斉qí, jì, zhāi\n"
	u8"건널 제濟济済jì, jǐ\n"
	u8"약제 제, 엄쪽 자劑剂剤jì\n"
	u8"가지 조條条条tiáo\n"
	u8"좇을 종從从従cóng\n"
	u8"세로 종, 바쁠 총縱纵縦zòng\n"
	u8"쇠 불릴 주鑄铸鋳zhù\n"
	u8"낮 주晝昼昼zhòu\n"
	u8"증거 증證证証zhèng\n"
	u8"더딜 지/늦을 지遲迟遅chí\n"
	u8"진압할 진, 메울 전鎭镇鎮\n"
	u8"다할 진盡尽尽jìn, jǐn\n"
	u8"부를 징, 음률 이름 치徵征徴zhēng, zhǐ\n"
	u8"도울 찬贊赞賛zàn\n"
	u8"참혹할 참慘惨惨cǎn\n"
	u8"참여할 참, 석 삼參参参cān, cēn, shēn\n"
	u8"곳 처處处処chù, chǔ\n"
	u8"얕을 천淺浅浅qiǎn, jiān\n"
	u8"밟을 천踐践践jiàn\n"
	u8"쇠 철鐵铁鉄tiě\n"
	u8"관청 청廳厅庁tīng\n"
	u8"들을 청聽听聴tīng\n"
	u8"몸 체體体体tǐ, tī\n"
	u8"막힐 체滯滞滞zhì\n"
	u8"갈릴 체, 두를 대遞递逓dì\n"
	u8"닿을 촉觸触触chù\n"
	u8"부탁할 촉囑嘱嘱zhǔ\n"
	u8"다 총/합할 총總总総zǒng, cōng\n"
	u8"귀 밝을 총聰聪聡cōng\n"
	u8"지도리 추, 나무 이름 우樞枢枢shū\n"
	u8"벌레 충, 벌레 훼, 찔 동蟲虫虫chóng\n"
	u8"이 치齒齿歯chǐ\n"
	u8"잘 침寢寝寝qǐn\n"
	u8"일컬을 칭/저울 칭稱称称chēng, chèn\n"
	u8"실을 타, 실을 태馱驮駄tuò, duò, tuó\n"
	u8"떨어질 타, 무너뜨릴 휴墮堕堕duò, huī\n"
	u8"탄알 탄彈弹弾dàn, tán\n"
	u8"가릴 택, 사람 이름 역擇择択zé, zhái\n"
	u8"못 택, 풀 석, 전국술 역, 별 이澤泽沢zé\n"
	u8"무너질 퇴/턱 퇴頹颓頽tuí\n"
	u8"폐할 폐/버릴 폐廢废廃fèi\n"
	u8"배울 학學学学xué\n"
	u8"시골 향鄕乡郷\n"
	u8"드릴 헌獻献献xiàn\n"
	u8"험할 험險险険xiǎn\n"
	u8"시험 험驗验験yàn\n"
	u8"나타날 현顯显顕xiǎn\n"
	u8"고을 현/매달 현縣县県xiàn\n"
	u8"뺨 협頰颊頬jiá\n"
	u8"골짜기 협峽峡峡xiá\n"
	u8"낄 협挾挟挟jiá, xiá\n"
	u8"좁을 협狹狭狭xiá\n"
	u8"의기로울 협俠侠侠xiá\n"
	u8"반딧불 형螢萤蛍yíng\n"
	u8"이름 호/부르짖을 호號号号hào, háo\n"
	u8"병 호壺壶壷hú\n"
	u8"넓힐 확, 북칠 황擴扩拡kuò\n"
	u8"기쁠 환歡欢歓huān\n"
	u8"품을 회懷怀懐huái\n"
	u8"모일 회會会会huì, kuài\n"
	u8"그림 회繪绘絵huì\n"
	u8"새벽 효曉晓暁xiǎo\n"
	u8"놀이 희, 서러울 호, 기 휘戲戏戯xì, hū\n"
	u8"희생 희, 술그릇 사犧牺犠xī\n"
	u8"\n"
	);

std::vector<std::shared_ptr<XivRes::MemoryMipmapStream>> GetFontTextures(const XivRes::GameReader& gameReader, const char* pcszTexturePathPattern) {
	std::vector<std::shared_ptr<XivRes::MemoryMipmapStream>> res;
	try {
		for (int i = 1; ; i++)
			res.emplace_back(XivRes::MemoryMipmapStream::AsARGB8888(*XivRes::TextureStream(gameReader.GetFileStream(std::format(pcszTexturePathPattern, i))).GetMipmap(0, 0)));
	} catch (const std::out_of_range&) {
		// do nothing
	}
	return res;
}

std::vector<std::shared_ptr<XivRes::FontGenerator::GameFontdataFixedSizeFont>> GetFonts(const XivRes::GameReader& gameReader, const char* const* ppcszFontdataPath, const char* pcszTexturePathPattern) {
	auto texs = GetFontTextures(gameReader, pcszTexturePathPattern);
	std::vector<std::shared_ptr<XivRes::FontGenerator::GameFontdataFixedSizeFont>> res;
	while (*ppcszFontdataPath) {
		res.emplace_back(std::make_shared<XivRes::FontGenerator::GameFontdataFixedSizeFont>(
			std::make_shared<XivRes::FontdataStream>(*gameReader.GetFileStream(*ppcszFontdataPath)),
			texs));
		ppcszFontdataPath++;
	}
	return res;
}

std::vector<std::shared_ptr<XivRes::FontGenerator::GameFontdataFixedSizeFont>> GetFontsGlo() {
	static const char* const fileList[]{
		"common/font/AXIS_96.fdt",
		"common/font/AXIS_12.fdt",
		"common/font/AXIS_14.fdt",
		"common/font/AXIS_18.fdt",
		"common/font/AXIS_36.fdt",
		"common/font/Jupiter_16.fdt",
		"common/font/Jupiter_20.fdt",
		"common/font/Jupiter_23.fdt",
		"common/font/Jupiter_45.fdt",
		"common/font/Jupiter_46.fdt",
		"common/font/Jupiter_90.fdt",
		"common/font/Meidinger_16.fdt",
		"common/font/Meidinger_20.fdt",
		"common/font/Meidinger_40.fdt",
		"common/font/MiedingerMid_10.fdt",
		"common/font/MiedingerMid_12.fdt",
		"common/font/MiedingerMid_14.fdt",
		"common/font/MiedingerMid_18.fdt",
		"common/font/MiedingerMid_36.fdt",
		"common/font/TrumpGothic_184.fdt",
		"common/font/TrumpGothic_23.fdt",
		"common/font/TrumpGothic_34.fdt",
		"common/font/TrumpGothic_68.fdt",
		nullptr,
	};
	XivRes::GameReader gameReader(R"(C:\Program Files (x86)\SquareEnix\FINAL FANTASY XIV - A Realm Reborn\game)");
	return GetFonts(gameReader, fileList, "common/font/font{}.tex");
}

std::vector<std::shared_ptr<XivRes::FontGenerator::GameFontdataFixedSizeFont>> GetFontsKrn() {
	static const char* const fileList[]{
		"common/font/KrnAXIS_120.fdt",
		"common/font/KrnAXIS_140.fdt",
		"common/font/KrnAXIS_180.fdt",
		"common/font/KrnAXIS_360.fdt",
		nullptr,
	};
	XivRes::GameReader gameReader(R"(C:\Program Files (x86)\FINAL FANTASY XIV - KOREA\game)");
	return GetFonts(gameReader, fileList, "common/font/font_krn_{}.tex");
}

std::vector<std::shared_ptr<XivRes::FontGenerator::GameFontdataFixedSizeFont>> GetFontsChn() {
	static const char* const fileList[]{
		"common/font/ChnAXIS_120.fdt",
		"common/font/ChnAXIS_140.fdt",
		"common/font/ChnAXIS_180.fdt",
		"common/font/ChnAXIS_360.fdt",
		nullptr,
	};
	XivRes::GameReader gameReader(R"(C:\Program Files (x86)\SNDA\FFXIV\game)");
	return GetFonts(gameReader, fileList, "common/font/font_chn_{}.tex");
}

int main() {
	system("chcp 65001");

	auto fontGlos = GetFontsGlo();
	auto fontKrns = GetFontsKrn();
	auto fontChns = GetFontsChn();

	std::set<char32_t> codepointsGlo, codepointsKrn, codepointsChn;
	codepointsGlo = fontGlos[0]->GetAllCodepoints();
	for (char32_t i = 'A'; i <= 'Z'; i += 3) {
		codepointsGlo.erase(i + 1);
		codepointsGlo.erase(i + 2);
	}
	for (char32_t i = 'a'; i <= 'z'; i += 3) {
		codepointsGlo.erase(i + 1);
		codepointsGlo.erase(i + 2);
	}
	for (char32_t i = '0'; i <= '9'; i += 3) {
		codepointsGlo.erase(i + 1);
		codepointsGlo.erase(i + 2);
	}
	std::ranges::set_difference(fontKrns[0]->GetAllCodepoints(), codepointsGlo, std::inserter(codepointsKrn, codepointsKrn.end()));
	for (char32_t i = 'A'; i <= 'Z'; i += 3) {
		codepointsKrn.erase(i + 2);
	}
	for (char32_t i = 'a'; i <= 'z'; i += 3) {
		codepointsKrn.erase(i + 2);
	}
	for (char32_t i = '0'; i <= '9'; i += 3) {
		codepointsKrn.erase(i + 2);
	}
	std::ranges::set_difference(fontChns[0]->GetAllCodepoints(), codepointsGlo, std::inserter(codepointsChn, codepointsChn.end()));

	XivRes::FontGenerator::FontdataPacker packer;
	packer.AddFont(fontGlos[0]);
	packer.AddFont(std::make_shared<XivRes::FontGenerator::MergedFixedSizeFont>(std::vector<std::shared_ptr<XivRes::FontGenerator::IFixedSizeFont>> {
		std::make_shared<XivRes::FontGenerator::CodepointLimitingFixedSizeFont>(fontGlos[1], codepointsGlo),
			std::make_shared<XivRes::FontGenerator::CodepointLimitingFixedSizeFont>(fontKrns[0], codepointsKrn),
			std::make_shared<XivRes::FontGenerator::CodepointLimitingFixedSizeFont>(fontChns[0], codepointsChn),
	}));
	packer.AddFont(std::make_shared<XivRes::FontGenerator::MergedFixedSizeFont>(std::vector<std::shared_ptr<XivRes::FontGenerator::IFixedSizeFont>> {
		std::make_shared<XivRes::FontGenerator::CodepointLimitingFixedSizeFont>(fontGlos[2], codepointsGlo),
			std::make_shared<XivRes::FontGenerator::CodepointLimitingFixedSizeFont>(fontKrns[1], codepointsKrn),
			std::make_shared<XivRes::FontGenerator::CodepointLimitingFixedSizeFont>(fontChns[1], codepointsChn),
	}));
	packer.AddFont(std::make_shared<XivRes::FontGenerator::MergedFixedSizeFont>(std::vector<std::shared_ptr<XivRes::FontGenerator::IFixedSizeFont>> {
		std::make_shared<XivRes::FontGenerator::CodepointLimitingFixedSizeFont>(fontGlos[3], codepointsGlo),
			std::make_shared<XivRes::FontGenerator::CodepointLimitingFixedSizeFont>(fontKrns[2], codepointsKrn),
			std::make_shared<XivRes::FontGenerator::CodepointLimitingFixedSizeFont>(fontChns[2], codepointsChn),
	}));
	packer.AddFont(std::make_shared<XivRes::FontGenerator::MergedFixedSizeFont>(std::vector<std::shared_ptr<XivRes::FontGenerator::IFixedSizeFont>> {
		std::make_shared<XivRes::FontGenerator::CodepointLimitingFixedSizeFont>(fontGlos[4], codepointsGlo),
			std::make_shared<XivRes::FontGenerator::CodepointLimitingFixedSizeFont>(fontKrns[3], codepointsKrn),
			std::make_shared<XivRes::FontGenerator::CodepointLimitingFixedSizeFont>(fontChns[3], codepointsChn),
	}));
	for (int i = 5; i < 23; i++) {
		packer.AddFont(fontGlos[i]);
	}
	dynamic_cast<XivRes::FontGenerator::MergedFixedSizeFont*>(packer.GetFont(1).get())->SetIndividualVerticalAdjustment(1, 1);
	dynamic_cast<XivRes::FontGenerator::MergedFixedSizeFont*>(packer.GetFont(4).get())->SetIndividualVerticalAdjustment(1, 1);
	dynamic_cast<XivRes::FontGenerator::MergedFixedSizeFont*>(packer.GetFont(1).get())->SetIndividualVerticalAdjustment(2, 1);
	dynamic_cast<XivRes::FontGenerator::MergedFixedSizeFont*>(packer.GetFont(4).get())->SetIndividualVerticalAdjustment(2, 1);

	{
		std::vector<std::thread> threads;
		
		//threads.emplace_back([&]() {
		//	auto face = packer.GetFont(0);
		//	XivRes::Internal::ShowTextureStream(*XivRes::FontGenerator::TextMeasurer(*face)
		//		.Measure(pszTestString)
		//		.CreateMipmap(*face, XivRes::RGBA8888(255, 255, 255, 255), XivRes::RGBA8888(0, 0, 0, 200))
		//		->ToSingleTextureStream(), L"AXIS_12");
		//});
		//threads.emplace_back([&]() {
		//	auto face = packer.GetFont(1);
		//	XivRes::Internal::ShowTextureStream(*XivRes::FontGenerator::TextMeasurer(*face)
		//		.Measure(pszTestString)
		//		.CreateMipmap(*face, XivRes::RGBA8888(255, 255, 255, 255), XivRes::RGBA8888(0, 0, 0, 200))
		//		->ToSingleTextureStream(), L"AXIS_14");
		//});
		//threads.emplace_back([&]() {
		//	auto face = packer.GetFont(2);
		//	XivRes::Internal::ShowTextureStream(*XivRes::FontGenerator::TextMeasurer(*face)
		//		.Measure(pszTestString)
		//		.CreateMipmap(*face, XivRes::RGBA8888(255, 255, 255, 255), XivRes::RGBA8888(0, 0, 0, 200))
		//		->ToSingleTextureStream(), L"AXIS_18");
		//});
		//threads.emplace_back([&]() {
		//	auto face = packer.GetFont(3);
		//	XivRes::Internal::ShowTextureStream(*XivRes::FontGenerator::TextMeasurer(*face)
		//		.Measure(pszTestString)
		//		.CreateMipmap(*face, XivRes::RGBA8888(255, 255, 255, 255), XivRes::RGBA8888(0, 0, 0, 200))
		//		->ToSingleTextureStream(), L"AXIS_36");
		//});

		//*
		auto [fdts, texs] = packer.Compile();
		auto res = std::make_shared<XivRes::TextureStream>(texs[0]->Type, texs[0]->Width, texs[0]->Height, 1, 1, texs.size());
		for (size_t i = 0; i < texs.size(); i++)
			res->SetMipmap(0, i, texs[i]);

		threads.emplace_back([&]() { XivRes::Internal::ShowTextureStream(*res); });

		auto face = std::make_shared<XivRes::FontGenerator::GameFontdataFixedSizeFont>(fdts[0], texs);
		threads.emplace_back([&]() {
			XivRes::Internal::ShowTextureStream(*XivRes::FontGenerator::TextMeasurer(*face)
				.Measure(pszTestString)
				.CreateMipmap(*face, XivRes::RGBA8888(255, 255, 255, 255), XivRes::RGBA8888(0, 0, 0, 200))
				->ToSingleTextureStream());
		});
		//*/

		for (auto& t : threads)
			t.join();
	}
	return 0;
}
