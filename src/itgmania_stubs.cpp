// itgmania_stubs.cpp
// Harness-local stubs to satisfy the small slice of ITGmania we compile for
// parsing and tech counting. These implementations are intentionally minimal
// and should only be used by the harness build.

#include "global.h"
#include "Attack.h"
#include "GameManager.h"
#include "GameState.h"
#include "MessageManager.h"
#include "PrefsManager.h"
#include "SongManager.h"
#include "RageFile.h"
#include "RageFileManager.h"
#include "RageLog.h"
#include "ScreenMessage.h"
#include "Song.h"
#include "Style.h"
#include "ThemeManager.h"
#include "BackgroundUtil.h"
#include "ActorUtil.h"
#include "LocalizedString.h"
#include "TechCounts.h"
#include "RadarValues.h"
#include "CryptManager.h"
#include "LuaBinding.h"
#include "LuaReference.h"
#include "LuaManager.h"
#include "EnumHelper.h"
#include "IniFile.h"
#include "NotesLoaderDWI.h"
#include "NotesLoaderKSF.h"
#include "NotesLoaderBMS.h"
#include "RageSoundReader_FileReader.h"
#include "RageTypes.h"
#include "arch/ArchHooks/ArchHooks.h"
#include "arch/Threads/Threads.h"

#include <tomcrypt.h>

#include <array>
#include <cctype>
#include <cerrno>
#include <cstring>
#include <cwctype>
#include <filesystem>
#include <fstream>
#include <map>
#include <memory>
#include <new>
#include <set>
#include <sstream>
#include <string>
#include <vector>
#include <ctime>
#include <cstdlib>

#include "crashhandler_stub.hpp"

// ---------------------------------------------------------------------------
// Globals
static RageLog gLog;
static GameState gGameState;
static GameManager gGameManager;
static ThemeManager gThemeManager;
static RageFileManager gFileManager("");
static MessageManager gMessageManager;
static SongManager gSongManager;

RageLog* LOG = &gLog;
GameState* GAMESTATE = &gGameState;
GameManager* GAMEMAN = &gGameManager;
PrefsManager* PREFSMAN = nullptr;
ThemeManager* THEME = &gThemeManager;
RageFileManager* FILEMAN = &gFileManager;
MessageManager* MESSAGEMAN = &gMessageManager;
LuaManager* LUA = nullptr;
SongManager* SONGMAN = &gSongManager;
// ScreenMessage constants provided by real ScreenMessage.cpp now.

// ---------------------------------------------------------------------------
// CrashHandler stubs (no-op in harness)
namespace CrashHandler {
void ForceCrash(char const*) {}
void ForceDeadlock(StdString::CStdStr<char>, uint64_t) {}
void ForceDeadlock(StdString::CStdStr<char> const&, uint64_t) {}
} // namespace CrashHandler

// ---------------------------------------------------------------------------
// my_localtime_r stub (fixes unresolved my_localtime_r on Windows/MSVC)
tm* my_localtime_r(time_t const* t, tm* out) {
	if (!t || !out) return nullptr;
#if defined(_WIN32)
	// localtime_s returns errno_t
	if (localtime_s(out, t) != 0) return nullptr;
	return out;
#else
	return localtime_r(t, out);
#endif
}

// ---------------------------------------------------------------------------
// RageLog (no-op)
RageLog::RageLog() = default;
RageLog::~RageLog() = default;

void RageLog::Trace(const char*, ...) {}
void RageLog::Warn(const char*, ...) {}
void RageLog::Info(const char*, ...) {}
void RageLog::Time(const char*, ...) {}
void RageLog::UserLog(const RString&, const RString&, const char*, ...) {}
void RageLog::Flush() {}
void RageLog::MapLog(const RString&, const char*, ...) {}
void RageLog::UnmapLog(const RString&) {}
const char* RageLog::GetAdditionalLog() { return nullptr; }
const char* RageLog::GetInfo() { return nullptr; }
const char* RageLog::GetRecentLog(int) { return nullptr; }
void RageLog::SetShowLogOutput(bool) {}
void RageLog::SetLogToDisk(bool) {}
void RageLog::SetInfoToDisk(bool) {}
void RageLog::SetUserLogToDisk(bool) {}
void RageLog::SetFlushing(bool) {}
void ShowWarningOrTrace(const char*, int, const RString&, bool) {}
void ShowWarningOrTrace(const char*, int, const char*, bool) {}

// ---------------------------------------------------------------------------
// Misc standalone helpers
static RString make_rstring(const char* s) { return RString(s); }
const RString& DifficultyToString(Difficulty d) {
	static RString beginner = make_rstring("beginner");
	static RString easy = make_rstring("easy");
	static RString medium = make_rstring("medium");
	static RString hard = make_rstring("hard");
	static RString challenge = make_rstring("challenge");
	static RString edit = make_rstring("edit");
	static RString invalid = make_rstring("invalid");
	switch (d) {
		case Difficulty_Beginner: return beginner;
		case Difficulty_Easy: return easy;
		case Difficulty_Medium: return medium;
		case Difficulty_Hard: return hard;
		case Difficulty_Challenge: return challenge;
		case Difficulty_Edit: return edit;
		default: return invalid;
	}
}
static Difficulty DifficultyFromString(const RString& in) {
	RString s(in);
	s.MakeLower();
	if (s == "beginner") return Difficulty_Beginner;
	if (s == "easy" || s == "light") return Difficulty_Easy;
	if (s == "medium" || s == "standard" || s == "normal") return Difficulty_Medium;
	if (s == "hard" || s == "heavy" || s == "difficult") return Difficulty_Hard;
	if (s == "challenge" || s == "oni" || s == "smaniac") return Difficulty_Challenge;
	if (s == "edit") return Difficulty_Edit;
	return Difficulty_Invalid;
}
Difficulty StringToDifficulty(const RString& s) { return DifficultyFromString(s); }
Difficulty OldStyleStringToDifficulty(const RString& s) { return DifficultyFromString(s); }
InstrumentTrack StringToInstrumentTrack(const RString&) { return InstrumentTrack_Invalid; }
const RString& InstrumentTrackToString(InstrumentTrack) { static RString empty; return empty; }

LocalizedString::LocalizedString(const RString&, const RString&) : m_pImpl(nullptr) {}
LocalizedString::LocalizedString(LocalizedString const&) : m_pImpl(nullptr) {}
LocalizedString::~LocalizedString() = default;
void LocalizedString::Load(const RString&, const RString&) {}
const RString& LocalizedString::GetValue() const { static RString empty; return empty; }
void LocalizedString::RegisterLocalizer(MakeLocalizer) {}
void LocalizedString::CreateImpl() {}

// ---------------------------------------------------------------------------
// Basic string helpers
#ifndef ITGMANIA_HARNESS_SOURCE
void MakeLower(char* p, size_t len) {
	if (!p) return;
	for (size_t i = 0; i < len && p[i]; ++i) {
		p[i] = static_cast<char>(std::tolower(static_cast<unsigned char>(p[i])));
	}
}
void MakeLower(wchar_t* p, size_t len) {
	if (!p) return;
	for (size_t i = 0; i < len && p[i]; ++i) {
		p[i] = static_cast<wchar_t>(std::towlower(p[i]));
	}
}
namespace StdString {
void ssasn(std::string& dst, const std::string& src) noexcept { dst = src; }
void ssasn(std::wstring& dst, const std::wstring& src) noexcept { dst = src; }
void ssasn(std::string& dst, const char* src) noexcept { dst = src ? src : ""; }
void ssasn(std::wstring& dst, const wchar_t* src) noexcept { dst = src ? src : L""; }
char sstolower(char ch) noexcept { return (ch >= 'A' && ch <= 'Z') ? static_cast<char>(ch + 'a' - 'A') : ch; }
wchar_t sstolower(wchar_t ch) noexcept { return (ch >= L'A' && ch <= L'Z') ? static_cast<wchar_t>(ch + L'a' - L'A') : ch; }
} // namespace StdString
#endif

RadarValues::RadarValues() { Zero(); }
void RadarValues::MakeUnknown() { FOREACH_ENUM(RadarCategory, rc) m_Values[rc] = RADAR_VAL_UNKNOWN; }
void RadarValues::Zero() { FOREACH_ENUM(RadarCategory, rc) m_Values[rc] = 0.0f; }
XNode* RadarValues::CreateNode(bool, bool) const { return nullptr; }
void RadarValues::LoadFromNode(const XNode*) {}
RString RadarValues::ToString(int) const { return ""; }
void RadarValues::FromString(RString) {}
ThemeMetric<bool> RadarValues::WRITE_SIMPLE_VALIES("", "");
ThemeMetric<bool> RadarValues::WRITE_COMPLEX_VALIES("", "");
void RadarValues::PushSelf(lua_State*) {}

#ifndef ITGMANIA_HARNESS_SOURCE
TechCounts::TechCounts() { Zero(); }
void TechCounts::MakeUnknown() { FOREACH_ENUM(TechCountsCategory, tc) m_Values[tc] = TECHCOUNTS_VAL_UNKNOWN; }
void TechCounts::Zero() { FOREACH_ENUM(TechCountsCategory, tc) m_Values[tc] = 0.0f; }
RString TechCounts::ToString(int) const { return ""; }
void TechCounts::FromString(RString) {}
void TechCounts::PushSelf(lua_State*) {}
void TechCounts::CalculateTechCountsFromRows(const std::vector<StepParity::Row>&, StepParity::StageLayout&, TechCounts& out) { out.Zero(); }
#endif

void Attack::GetAttackBeats(const Song*, float& fStartBeat, float& fEndBeat) const { fStartBeat = 0.0f; fEndBeat = 0.0f; }
void Attack::GetRealtimeAttackBeats(const Song*, const PlayerState*, float& fStartBeat, float& fEndBeat) const { fStartBeat = 0.0f; fEndBeat = 0.0f; }
bool Attack::operator==(const Attack& rhs) const { return sModifiers == rhs.sModifiers && fStartSecond == rhs.fStartSecond && fSecsRemaining == rhs.fSecsRemaining; }
bool Attack::ContainsTransformOrTurn() const { return false; }
Attack Attack::FromGlobalCourseModifier(const RString& mods) { return Attack(ATTACK_LEVEL_1, ATTACK_STARTS_NOW, 0.0f, mods, false, true); }
RString Attack::GetTextDescription() const { return sModifiers; }
int Attack::GetNumAttacks() const { return 0; }

void PlayerOptions::FromString(const RString&) {}
bool PlayerOptions::ContainsTransformOrTurn() const { return false; }

RString CryptManager::GetSHA1ForString(RString s) {
	hash_state hs;
	unsigned char out[20];
	sha1_init(&hs);
	sha1_process(&hs, reinterpret_cast<const unsigned char*>(s.data()), static_cast<unsigned long>(s.size()));
	sha1_done(&hs, out);
	static const char hex[] = "0123456789abcdef";
	RString result;
	result.reserve(sizeof(out) * 2);
	for (unsigned char b : out) {
		result.push_back(hex[b >> 4]);
		result.push_back(hex[b & 0x0F]);
	}
	return result;
}

int64_t ArchHooks::GetSystemTimeInMicroseconds() { return 0; }

// ---------------------------------------------------------------------------
// RageTimer minimal implementation
#ifndef ITGMANIA_HARNESS_SOURCE
const RageTimer RageZeroTimer;

float RageTimer::Ago() const { return 0.0f; }
void RageTimer::Touch() { m_secs = 0; m_us = 0; }
float RageTimer::GetDeltaTime() { Touch(); return 0.0f; }
double RageTimer::GetTimeSinceStart() { return 0.0; }
int RageTimer::GetTimeSinceStartSeconds() { return 0; }
uint64_t RageTimer::GetTimeSinceStartMicroseconds() { return 0; }
RageTimer RageTimer::Half() const { return *this; }
RageTimer RageTimer::operator+(float) const { return *this; }
float RageTimer::operator-(const RageTimer&) const { return 0.0f; }
bool RageTimer::operator<(const RageTimer& rhs) const {
	return m_secs < rhs.m_secs || (m_secs == rhs.m_secs && m_us < rhs.m_us);
}
#endif

LuaBinding::LuaBinding() = default;
LuaBinding::~LuaBinding() = default;
void LuaBinding::Register(lua_State*) {}
void LuaBinding::RegisterTypes(lua_State*) {}
void LuaBinding::ApplyDerivedType(Lua*, const RString&, void*) {}
void* LuaBinding::GetPointerFromStack(Lua*, const RString&, int) { return nullptr; }
void LuaBinding::CreateMethodsTable(lua_State*, const RString&) {}
bool LuaBinding::Equal(lua_State*) { return false; }
int LuaBinding::PushEqual(lua_State* L) { lua_pushboolean(L, 0); return 1; }
bool LuaBinding::CheckLuaObjectType(lua_State*, int, const char*) { return true; }
int LuaHelpers::TypeError(Lua*, int, const char*) { return 0; }

void IPreference::SetFromStack(lua_State*) {}
void IPreference::PushValue(lua_State*) const {}

// Minimal LuaManager / LuaReference scaffolding for Lua registration macros.
LuaManager::LuaManager() : m_pLuaMain(nullptr) {}
LuaManager::~LuaManager() = default;
void LuaManager::Register(RegisterWithLuaFn) {}
Lua* LuaManager::Get() { return nullptr; }
void LuaManager::Release(Lua*& p) { p = nullptr; }
void LuaManager::YieldLua() {}
void LuaManager::UnyieldLua() {}
void LuaManager::RegisterTypes() {}
void LuaManager::SetGlobal(const RString&, int) {}
void LuaManager::SetGlobal(const RString&, const RString&) {}
void LuaManager::UnsetGlobal(const RString&) {}

LuaReference::LuaReference() : m_iReference(LUA_NOREF) {}
LuaReference::~LuaReference() = default;
LuaReference::LuaReference(const LuaReference&) : m_iReference(LUA_NOREF) {}
LuaReference& LuaReference::operator=(const LuaReference&) = default;
void LuaReference::SetFromStack(Lua*) { m_iReference = LUA_NOREF; }
void LuaReference::SetFromNil() { m_iReference = LUA_REFNIL; }
bool LuaReference::SetFromExpression(const RString&) { m_iReference = LUA_NOREF; return true; }
void LuaReference::DeepCopy() {}
void LuaReference::PushSelf(lua_State* L) const { lua_pushnil(L); }
bool LuaReference::IsSet() const { return m_iReference != LUA_NOREF; }
bool LuaReference::IsNil() const { return m_iReference == LUA_REFNIL; }
int LuaReference::GetLuaType() const { return LUA_TNIL; }
RString LuaReference::Serialize() const { return ""; }
void LuaReference::Unregister() { m_iReference = LUA_NOREF; }
LuaTable::LuaTable() : LuaReference() {}
void LuaTable::Set(Lua*, const RString&) {}
void LuaTable::Get(Lua*, const RString&) {}

int CheckEnum(lua_State*, LuaReference&, int, int iInvalid, const char*, bool, bool) { return iInvalid; }
const RString& EnumToString(int, int, const char**, std::unique_ptr<RString>*) {
	static RString empty;
	return empty;
}
void Enum::SetMetatable(lua_State*, LuaReference&, LuaReference&, const char*) {}
template<> LuaReference EnumTraits<PlayerNumber>::StringToEnum;
template<> LuaReference EnumTraits<PlayerNumber>::EnumToString;
template<> PlayerNumber EnumTraits<PlayerNumber>::Invalid = PLAYER_INVALID;
template<> const char* EnumTraits<PlayerNumber>::szName = "PlayerNumber";

namespace LuaHelpers {
template<> void Push<float>(lua_State* L, float const& v) { lua_pushnumber(L, v); }
template<> void Push<bool>(lua_State* L, bool const& v) { lua_pushboolean(L, v); }
template<> void Push<int>(lua_State* L, int const& v) { lua_pushinteger(L, v); }
template<> void Push<double>(lua_State* L, double const& v) { lua_pushnumber(L, v); }
template<> void Push<Difficulty>(lua_State* L, Difficulty const& v) { lua_pushinteger(L, v); }
template<> void Push<PlayerNumber>(lua_State* L, PlayerNumber const& v) { lua_pushinteger(L, v); }
template<> void Push<RString>(lua_State* L, RString const& v) { lua_pushlstring(L, v.data(), v.size()); }
template<> void Push<std::string>(lua_State* L, std::string const& v) { lua_pushlstring(L, v.data(), v.size()); }
template<> void Push<RageColor>(lua_State* L, RageColor const&) { lua_newtable(L); }

template<> bool FromStack<float>(lua_State* L, float& out, int i) { out = static_cast<float>(lua_tonumber(L, i)); return true; }
template<> bool FromStack<bool>(lua_State* L, bool& out, int i) { out = lua_toboolean(L, i) != 0; return true; }
template<> bool FromStack<int>(lua_State* L, int& out, int i) { out = static_cast<int>(lua_tointeger(L, i)); return true; }
template<> bool FromStack<RString>(lua_State* L, RString& out, int i) { size_t len = 0; const char* s = lua_tolstring(L, i, &len); out.assign(s ? s : "", len); return true; }
template<> bool FromStack<std::string>(lua_State* L, std::string& out, int i) { size_t len = 0; const char* s = lua_tolstring(L, i, &len); out.assign(s ? s : "", len); return true; }
template<> bool FromStack<RageColor>(lua_State*, RageColor& out, int) { out = RageColor(); return true; }

void PushValueFunc(lua_State*, int) {}
bool RunExpression(Lua*, const RString&, const RString&) { return false; }
void DeepCopy(lua_State*) {}

void CreateTableFromArrayB(Lua* L, const std::vector<bool>& vals) {
	lua_newtable(L);
	int idx = 1;
	for (bool v : vals) {
		lua_pushboolean(L, v);
		lua_rawseti(L, -2, idx++);
	}
}
} // namespace LuaHelpers

bool DoesFileExist(const RString& path) { return FILEMAN && FILEMAN->DoesFileExist(path); }
bool IsAFile(const RString& path) { return FILEMAN && FILEMAN->IsAFile(path); }
bool IsADirectory(const RString& path) { return FILEMAN && FILEMAN->IsADirectory(path); }
void GetDirListing(const RString& path, std::vector<RString>& out, bool onlyDirs, bool returnPathToo) {
	if (FILEMAN) FILEMAN->GetDirListing(path, out, onlyDirs, returnPathToo);
	else out.clear();
}

RageSoundReader_FileReader* RageSoundReader_FileReader::OpenFile(RString, RString& error, bool*) { error = ""; return nullptr; }

ThreadImpl* MakeThread(int (*)(void*), void*, uint64_t* piThreadID) { if (piThreadID) *piThreadID = 0; return nullptr; }
ThreadImpl* MakeThisThread() { return nullptr; }
MutexImpl* MakeMutex(RageMutex*) { return nullptr; }
EventImpl* MakeEvent(MutexImpl*) { return nullptr; }
SemaImpl* MakeSemaphore(int) { return nullptr; }
uint64_t GetThisThreadId() { return 0; }
uint64_t GetInvalidThreadId() { return 0; }

// ---------------------------------------------------------------------------
// Minimal RageFile backed by std::ifstream
class RageFileStd final : public RageFileBasic {
  public:
	RageFileStd() : m_size(-1) {}
	explicit RageFileStd(const RString& path) : m_path(path), m_size(-1) {}

	RageFileBasic* Copy() const override { return new RageFileStd(m_path); }
	RString GetDisplayPath() const override { return m_path; }
	RString GetError() const override { return m_error; }
	void ClearError() override { m_error.clear(); }
	bool AtEOF() const override { return !m_stream || m_stream->eof(); }

	int Seek(int offset) override {
		if (!m_stream) return -1;
		m_stream->clear();
		m_stream->seekg(offset, std::ios::beg);
		return static_cast<int>(m_stream->tellg());
	}
	int Seek(int offset, int whence) override {
		if (!m_stream) return -1;
		std::ios::seekdir dir = std::ios::beg;
		if (whence == std::ios::cur) dir = std::ios::cur;
		else if (whence == std::ios::end) dir = std::ios::end;
		m_stream->clear();
		m_stream->seekg(offset, dir);
		return static_cast<int>(m_stream->tellg());
	}
	int Tell() const override {
		if (!m_stream) return -1;
		return static_cast<int>(m_stream->tellg());
	}

	int Read(void* buffer, size_t bytes) override {
		if (!m_stream) return -1;
		m_stream->read(static_cast<char*>(buffer), static_cast<std::streamsize>(bytes));
		return static_cast<int>(m_stream->gcount());
	}
	int Read(RString& buffer, int bytes = -1) override {
		if (!m_stream) return -1;
		if (bytes < 0) {
			std::ostringstream ss;
			ss << m_stream->rdbuf();
			buffer = ss.str();
			return static_cast<int>(buffer.size());
		}
		buffer.resize(bytes);
		m_stream->read(buffer.data(), bytes);
		return static_cast<int>(m_stream->gcount());
	}
	int Read(void* buffer, size_t bytes, int nmemb) override {
		return Read(buffer, bytes * static_cast<size_t>(nmemb));
	}

	int Write(const void*, size_t) override { return -1; }
	int Write(const RString&) override { return -1; }
	int Write(const void*, size_t, int) override { return -1; }
	int Flush() override { return 0; }

	int GetLine(RString& out) override {
		if (!m_stream) return -1;
		std::string line;
		if (!std::getline(*m_stream, line)) return 0;
		out = line;
		return static_cast<int>(out.size());
	}
	int PutLine(const RString&) override { return -1; }

	void EnableCRC32(bool) override {}
	bool GetCRC32(uint32_t*) override { return false; }

	int GetFileSize() const override {
		if (m_size >= 0) return m_size;
		if (!m_stream) return -1;
		auto cur = m_stream->tellg();
		m_stream->seekg(0, std::ios::end);
		m_size = static_cast<int>(m_stream->tellg());
		m_stream->seekg(cur);
		return m_size;
	}
	int GetFD() override { return -1; }

	bool Open(const RString& path, int /*mode*/) {
		m_path = path;
		m_error.clear();
		m_stream.reset(new std::ifstream(path.c_str(), std::ios::binary));
		if (!*m_stream) {
			m_error = "open failed";
			m_stream.reset();
			return false;
		}
		m_size = -1;
		return true;
	}

  private:
	RString m_path;
	mutable int m_size;
	RString m_error;
	std::unique_ptr<std::ifstream> m_stream;
};

RageFile::RageFile() : m_File(nullptr), m_Mode(0) {}
RageFile::RageFile(const RageFile& cpy) : RageFileBasic(cpy) {
	m_File = nullptr;
	m_Path = cpy.m_Path;
	m_Mode = cpy.m_Mode;
	if (cpy.m_File) {
		m_File = cpy.m_File->Copy();
	}
}
RageFile* RageFile::Copy() const { return new RageFile(*this); }
RString RageFile::GetPath() const { return m_Path; }
bool RageFile::Open(const RString& path, int mode) {
	Close();
	if (!(mode & READ)) {
		SetError("write unsupported in harness");
		return false;
	}
	m_Mode = mode;
	auto* f = new RageFileStd;
	if (!f->Open(path, mode)) {
		SetError(f->GetError());
		delete f;
		return false;
	}
	m_File = f;
	m_Path = path;
	return true;
}
void RageFile::Close() {
	if (m_File) {
		delete m_File;
		m_File = nullptr;
	}
}
int RageFile::GetLine(RString& out) { return m_File ? m_File->GetLine(out) : -1; }
int RageFile::PutLine(const RString&) { return -1; }
void RageFile::EnableCRC32(bool on) { if (m_File) m_File->EnableCRC32(on); }
bool RageFile::GetCRC32(uint32_t* out) { return m_File && m_File->GetCRC32(out); }
int RageFile::Read(void* buffer, size_t bytes) { return m_File ? m_File->Read(buffer, bytes) : -1; }
int RageFile::Read(RString& buffer, int bytes) { return m_File ? m_File->Read(buffer, bytes) : -1; }
int RageFile::Read(void* buffer, size_t bytes, int nmemb) {
	return m_File ? m_File->Read(buffer, bytes, nmemb) : -1;
}
int RageFile::Write(const void*, size_t) { return -1; }
int RageFile::Write(const void*, size_t, int) { return -1; }
int RageFile::Flush() { return 0; }
int RageFile::Seek(int offset) { return m_File ? m_File->Seek(offset) : -1; }
int RageFile::Seek(int offset, int whence) { return m_File ? m_File->Seek(offset, whence) : -1; }
int RageFile::Tell() const { return m_File ? m_File->Tell() : -1; }
int RageFile::GetFileSize() const { return m_File ? m_File->GetFileSize() : -1; }
int RageFile::GetFD() { return -1; }
RString RageFile::GetError() const { return m_sError; }
void RageFile::ClearError() { m_sError = ""; }
bool RageFile::AtEOF() const { return m_File ? m_File->AtEOF() : true; }
void RageFile::SetError(const RString& err) { m_sError = err; }
void RageFile::PushSelf(lua_State*) {}

// ---------------------------------------------------------------------------
// RageFileManager (std::filesystem backed)
RageFileManager::RageFileManager(const RString&) {}
RageFileManager::~RageFileManager() {}
void RageFileManager::MountInitialFilesystems() {}
void RageFileManager::MountUserFilesystems() {}
void RageFileManager::GetDirListing(const RString& path, std::vector<RString>& out, bool onlyDirs, bool returnPathToo) {
	using std::filesystem::directory_iterator;
	out.clear();
	std::error_code ec;
	for (const auto& entry : directory_iterator(path.c_str(), ec)) {
		if (ec) break;
		if (onlyDirs && !entry.is_directory()) continue;
		RString name = returnPathToo ? entry.path().string().c_str()
		                             : entry.path().filename().string().c_str();
		out.push_back(name);
	}
}
void RageFileManager::GetDirListingWithMultipleExtensions(
	const RString& path,
	std::vector<RString> const& exts,
	std::vector<RString>& out,
	bool onlyDirs,
	bool returnPathToo)
{
	using std::filesystem::directory_iterator;
	out.clear();
	std::error_code ec;
	for (const auto& entry : directory_iterator(path.c_str(), ec)) {
		if (ec) break;
		if (onlyDirs && !entry.is_directory()) continue;
		if (!exts.empty()) {
			auto ext = entry.path().extension().string();
			bool match = false;
			for (const auto& e : exts) {
				if (ext == e.c_str()) { match = true; break; }
			}
			if (!match) continue;
		}
		RString name = returnPathToo ? entry.path().string().c_str()
		                             : entry.path().filename().string().c_str();
		out.push_back(name);
	}
}
bool RageFileManager::Move(const RString&, const RString&) { return false; }
bool RageFileManager::Copy(const std::string&, const std::string&) { return false; }
bool RageFileManager::Remove(const RString&) { return false; }
bool RageFileManager::DeleteRecursive(const RString&) { return false; }
void RageFileManager::CreateDir(const RString&) {}
RageFileManager::FileType RageFileManager::GetFileType(const RString& path) {
	std::error_code ec;
	auto status = std::filesystem::status(path.c_str(), ec);
	if (ec) return TYPE_NONE;
	if (std::filesystem::is_directory(status)) return TYPE_DIR;
	if (std::filesystem::is_regular_file(status)) return TYPE_FILE;
	return TYPE_NONE;
}
bool RageFileManager::IsAFile(const RString& path) { return GetFileType(path) == TYPE_FILE; }
bool RageFileManager::IsADirectory(const RString& path) { return GetFileType(path) == TYPE_DIR; }
bool RageFileManager::DoesFileExist(const RString& path) { return IsAFile(path); }
int RageFileManager::GetFileSizeInBytes(const RString& path) {
	std::error_code ec;
	auto sz = std::filesystem::file_size(path.c_str(), ec);
	return ec ? -1 : static_cast<int>(sz);
}
int RageFileManager::GetFileHash(const RString&) { return 0; }
RString RageFileManager::ResolvePath(const RString& path) { return path; }
bool RageFileManager::Mount(const RString&, const RString&, const RString&) { return true; }
void RageFileManager::Unmount(const RString&, const RString&, const RString&) {}
void RageFileManager::Remount(RString, RString) {}
bool RageFileManager::IsMounted(RString) { return true; }
void RageFileManager::GetLoadedDrivers(std::vector<DriverLocation>&) {}
void RageFileManager::FlushDirCache(const RString&) {}
RageFileBasic* RageFileManager::Open(const RString& path, int mode, int& error) {
	auto* f = new RageFileStd;
	if (!f->Open(path, mode)) {
		error = errno;
		delete f;
		return nullptr;
	}
	error = 0;
	return f;
}
void RageFileManager::CacheFile(const RageFileBasic*, const RString&) {}
RageFileDriver* RageFileManager::GetFileDriver(RString) { return nullptr; }
void RageFileManager::ReleaseFileDriver(RageFileDriver*) {}
bool RageFileManager::Unzip(const std::string&, std::string, int) { return false; }
void RageFileManager::ProtectPath(const std::string&) {}
bool RageFileManager::IsPathProtected(const std::string&) { return false; }
void RageFileManager::PushSelf(lua_State*) {}

namespace RageFileManagerUtil {
RString sDirOfExecutable;
}

bool ilt(const RString& a, const RString& b) { return a.CompareNoCase(b) < 0; }
bool ieq(const RString& a, const RString& b) { return a.CompareNoCase(b) == 0; }

// ---------------------------------------------------------------------------
// Preference/IPreference stubs (avoid full Preference.cpp dependency)
IPreference::IPreference(const RString&, PreferenceType) : m_sName(""), m_bDoNotWrite(false), m_bImmutable(false) {}
IPreference::~IPreference() = default;
void IPreference::ReadFrom(const XNode*, bool) {}
void IPreference::WriteTo(XNode*) const {}
void IPreference::ReadDefaultFrom(const XNode*) {}
IPreference* IPreference::GetPreferenceByName(const RString&) { return nullptr; }
void IPreference::LoadAllDefaults() {}
void IPreference::ReadAllPrefsFromNode(const XNode*, bool) {}
void IPreference::SavePrefsToNode(XNode*) {}
void IPreference::ReadAllDefaultsFromNode(const XNode*) {}
void BroadcastPreferenceChanged(const RString&) {}

// ---------------------------------------------------------------------------
// GameState / GameManager / ThemeManager stubs
GameState::GameState()
	: masterPlayerNumber(PLAYER_1),
	  processedTiming(nullptr),
	  m_pCurGame(MessageID_Invalid),
	  m_pCurStyle(MessageID_Invalid),
	  m_PlayMode(MessageID_Invalid),
	  m_iCoins(MessageID_Invalid),
	  m_bMultiplayer(false),
	  m_iNumMultiplayerNoteFields(0),
	  m_timeGameStarted(),
	  m_Environment(nullptr),
	  m_iGameSeed(0),
	  m_iStageSeed(0),
	  m_sPreferredSongGroup(MessageID_Invalid),
	  m_sPreferredCourseGroup(MessageID_Invalid),
	  m_bFailTypeWasExplicitlySet(false),
	  m_PreferredStepsType(MessageID_Invalid),
	  m_PreferredDifficulty(MessageID_Invalid),
	  m_PreferredCourseDifficulty(MessageID_Invalid),
	  m_SortOrder(MessageID_Invalid),
	  m_PreferredSortOrder(SortOrder_Invalid),
	  m_EditMode(EditMode_Invalid),
	  m_bDemonstrationOrJukebox(false),
	  m_bJukeboxUsesModifiers(false),
	  m_iNumStagesOfThisSong(0),
	  m_iCurrentStageIndex(0),
	  m_AdjustTokensBySongCostForFinalStageCheck(false),
	  m_bLoadingNextSong(false),
	  m_pCurSong(MessageID_Invalid),
	  m_pPreferredSong(nullptr),
	  m_pCurSteps(MessageID_Invalid),
	  m_pCurCourse(MessageID_Invalid),
	  m_pCurTrail(MessageID_Invalid),
	  m_bGameplayLeadIn(MessageID_Invalid),
	  m_stEdit(MessageID_Invalid),
	  m_cdEdit(MessageID_Invalid),
	  m_pEditSourceSteps(MessageID_Invalid),
	  m_stEditSource(MessageID_Invalid),
	  m_iEditCourseEntryIndex(MessageID_Invalid),
	  m_sEditLocalProfileID(MessageID_Invalid)
{
	for (auto& s : m_SeparatedStyles) s = nullptr;
	for (bool& joined : m_bSideIsJoined) joined = false;
	for (auto& status : m_MultiPlayerStatus) status = MultiPlayerStatus_NotJoined;
	m_SongOptions.Init();
	for (int& tokens : m_iPlayerStageTokens) tokens = 0;
}
GameState::~GameState() = default;
void GameState::Reset() {}
void GameState::ResetPlayer(PlayerNumber) {}
void GameState::ResetPlayerOptions(PlayerNumber) {}
void GameState::ApplyCmdline() {}
void GameState::ApplyGameCommand(const RString&, PlayerNumber) {}
void GameState::BeginGame() {}
void GameState::JoinPlayer(PlayerNumber) {}
void GameState::UnjoinPlayer(PlayerNumber) {}
bool GameState::JoinInput(PlayerNumber) { return false; }
bool GameState::JoinPlayers() { return false; }
void GameState::LoadProfiles(bool) {}
void GameState::SavePlayerProfiles() {}
void GameState::SavePlayerProfile(PlayerNumber) {}
bool GameState::HaveProfileToLoad() { return false; }
bool GameState::HaveProfileToSave() { return false; }
void GameState::SaveLocalData() {}
void GameState::AddStageToPlayer(PlayerNumber) {}
void GameState::LoadCurrentSettingsFromProfile(PlayerNumber) {}
void GameState::SaveCurrentSettingsToProfile(PlayerNumber) {}
Song* GameState::GetDefaultSong() const { return nullptr; }
bool GameState::CanSafelyEnterGameplay(RString&) { return true; }
void GameState::SetCompatibleStylesForPlayers() {}
void GameState::ForceSharedSidesMatch() {}
void GameState::ForceOtherPlayersToCompatibleSteps(PlayerNumber) {}
void GameState::Update(float) {}
void GameState::SetCurGame(const Game*) {}
bool GameState::DifficultiesLocked() const { return false; }
bool GameState::ChangePreferredDifficultyAndStepsType(PlayerNumber, Difficulty, StepsType) { return false; }
bool GameState::ChangePreferredDifficulty(PlayerNumber, int) { return false; }
bool GameState::ChangePreferredCourseDifficultyAndStepsType(PlayerNumber, CourseDifficulty, StepsType) { return false; }
bool GameState::ChangePreferredCourseDifficulty(PlayerNumber, int) { return false; }
const Style* GameState::GetCurrentStyle(PlayerNumber) const { return nullptr; }
void GameState::SetProcessedTimingData(TimingData* td) { processedTiming = td; }
TimingData* GameState::GetProcessedTimingData() const { return processedTiming; }
bool GameState::IsCourseDifficultyShown(CourseDifficulty) { return true; }

GameManager::GameManager() = default;
GameManager::~GameManager() = default;
void GameManager::GetStylesForGame(const Game*, std::vector<const Style*>&, bool) {}
const Game* GameManager::GetGameForStyle(const Style*) { return nullptr; }
void GameManager::GetStepsTypesForGame(const Game*, std::vector<StepsType>&) {}
const Style* GameManager::GetEditorStyleForStepsType(StepsType) { return nullptr; }
void GameManager::GetDemonstrationStylesForGame(const Game*, std::vector<const Style*>&) {}
const Style* GameManager::GetHowToPlayStyleForGame(const Game*) { return nullptr; }
void GameManager::GetCompatibleStyles(const Game*, int, std::vector<const Style*>&) {}
const Style* GameManager::GetFirstCompatibleStyle(const Game*, int, StepsType) { return nullptr; }
void GameManager::GetEnabledGames(std::vector<const Game*>&) {}
const Game* GameManager::GetDefaultGame() { return nullptr; }
bool GameManager::IsGameEnabled(const Game*) { return true; }
int GameManager::GetIndexFromGame(const Game*) { return 0; }
const Game* GameManager::GetGameFromIndex(int) { return nullptr; }
const StepsTypeInfo& GameManager::GetStepsTypeInfo(StepsType st) {
	static StepsTypeInfo info[] = {
		{"dance-single", 4, true, StepsTypeCategory_Single},
		{"dance-double", 8, true, StepsTypeCategory_Double},
		{"lights-cabinet", 8, true, StepsTypeCategory_Single},
	};
	int idx = 0;
	switch (st) {
		case StepsType_dance_single: idx = 0; break;
		case StepsType_dance_double: idx = 1; break;
		case StepsType_lights_cabinet: idx = 2; break;
		default: idx = 0; break;
	}
	return info[idx];
}
StepsType GameManager::StringToStepsType(RString s) {
	s.MakeLower();
	if (s == "dance-single" || s == "dance_single") return StepsType_dance_single;
	if (s == "dance-double" || s == "dance_double") return StepsType_dance_double;
	if (s == "lights-cabinet" || s == "lights_cabinet") return StepsType_lights_cabinet;
	return StepsType_Invalid;
}
const Game* GameManager::StringToGame(RString) { return nullptr; }
const Style* GameManager::GameAndStringToStyle(const Game*, RString) { return nullptr; }
RString GameManager::StyleToLocalizedString(const Style*) { return ""; }
void GameManager::PushSelf(lua_State*) {}

SongManager::SongManager() = default;
SongManager::~SongManager() = default;
Song* SongManager::FindSong(RString) const { return nullptr; }
Song* SongManager::FindSong(RString, RString) const { return nullptr; }

RageTexturePreloader::~RageTexturePreloader() = default;
RageTexturePreloader& RageTexturePreloader::operator=(const RageTexturePreloader&) { return *this; }
void RageTexturePreloader::Load(const RageTextureID&) {}
void RageTexturePreloader::UnloadAll() {}

ThemeManager::ThemeManager() = default;
ThemeManager::~ThemeManager() = default;
// ... (UNCHANGED from your paste: all the ThemeManager methods you included)
bool ThemeManager::DoesThemeExist(const RString&) { return false; }
void ThemeManager::GetThemeNames(std::vector<RString>&) {}
void ThemeManager::GetSelectableThemeNames(std::vector<RString>&) {}
int ThemeManager::GetNumSelectableThemes() { return 0; }
bool ThemeManager::IsThemeSelectable(RString const&) { return false; }
bool ThemeManager::IsThemeNameValid(RString const&) { return false; }
RString ThemeManager::GetThemeDisplayName(const RString&) { return ""; }
RString ThemeManager::GetThemeAuthor(const RString&) { return ""; }
void ThemeManager::GetLanguages(std::vector<RString>&) {}
bool ThemeManager::DoesLanguageExist(const RString&) { return false; }
void ThemeManager::SwitchThemeAndLanguage(const RString&, const RString&, bool, bool) {}
void ThemeManager::UpdateLuaGlobals() {}
RString ThemeManager::GetNextTheme() { return ""; }
RString ThemeManager::GetNextSelectableTheme() { return ""; }
void ThemeManager::ReloadMetrics() {}
void ThemeManager::ReloadSubscribers() {}
void ThemeManager::ClearSubscribers() {}
void ThemeManager::GetOptionNames(std::vector<RString>&) {}
bool ThemeManager::GetPathInfo(PathInfo&, ElementCategory, const RString&, const RString&, bool) { return false; }
RString ThemeManager::GetPath(ElementCategory, const RString&, const RString&, bool) { return ""; }
void ThemeManager::ClearThemePathCache() {}
bool ThemeManager::HasMetric(const RString&, const RString&) { return false; }
void ThemeManager::Subscribe(IThemeMetric*) {}
void ThemeManager::Unsubscribe(IThemeMetric*) {}
void ThemeManager::PushMetric(Lua*, const RString&, const RString&) {}
RString ThemeManager::GetMetric(const RString&, const RString&) { return ""; }
int ThemeManager::GetMetricI(const RString&, const RString&) { return 0; }
float ThemeManager::GetMetricF(const RString&, const RString&) { return 0.0f; }
bool ThemeManager::GetMetricB(const RString&, const RString&) { return false; }
RageColor ThemeManager::GetMetricC(const RString&, const RString&) { return RageColor(); }
LuaReference ThemeManager::GetMetricR(const RString&, const RString&) { return LuaReference(); }
void ThemeManager::GetMetric(const RString&, const RString&, LuaReference&) {}
bool ThemeManager::HasString(const RString&, const RString&) { return false; }
RString ThemeManager::GetString(const RString&, const RString&) { return ""; }
void ThemeManager::FilterFileLanguages(std::vector<RString>&) {}
void ThemeManager::GetMetricsThatBeginWith(const RString&, const RString&, std::set<RString>&) {}
RString ThemeManager::GetMetricsGroupFallback(const RString&) { return ""; }
RString ThemeManager::GetBlankGraphicPath() { return ""; }
void ThemeManager::RunLuaScripts(const RString&, bool) {}
void ThemeManager::PushSelf(lua_State*) {}

// ---------------------------------------------------------------------------
// MessageManager minimal
MessageManager::MessageManager() = default;
MessageManager::~MessageManager() = default;
void MessageManager::Subscribe(IMessageSubscriber*, const RString&) {}
void MessageManager::Subscribe(IMessageSubscriber*, MessageID) {}
void MessageManager::Unsubscribe(IMessageSubscriber*, const RString&) {}
void MessageManager::Unsubscribe(IMessageSubscriber*, MessageID) {}
void MessageManager::Broadcast(Message&) const {}
void MessageManager::Broadcast(const RString&) const {}
void MessageManager::Broadcast(MessageID) const {}
bool MessageManager::IsSubscribedToMessage(IMessageSubscriber*, const RString&) const { return false; }
void MessageManager::PushSelf(lua_State*) {}

// ---------------------------------------------------------------------------
// Minimal chart structures
#ifndef ITGMANIA_HARNESS_SOURCE
void DisplayBpms::Add(float f) { vfBpms.push_back(f); }
float DisplayBpms::GetMin() const {
	if (vfBpms.empty()) return 0.0f;
	float out = vfBpms.front();
	for (float v : vfBpms) if (v < out) out = v;
	return out;
}
float DisplayBpms::GetMax() const {
	if (vfBpms.empty()) return 0.0f;
	float out = vfBpms.front();
	for (float v : vfBpms) if (v > out) out = v;
	return out;
}
float DisplayBpms::GetMaxWithin(float highest) const {
	float out = GetMax();
	return out > highest ? highest : out;
}
bool DisplayBpms::BpmIsConstant() const { return vfBpms.size() <= 1 || GetMin() == GetMax(); }
bool DisplayBpms::IsSecret() const { return false; }

TimingData::TimingData(float fOffset)
	: m_fBeat0OffsetInSeconds(fOffset),
	  m_fBeat0GroupOffsetInSeconds(0.0f) {}

TimingData::~TimingData() {
	for (auto& vec : m_avpTimingSegments) {
		for (auto* seg : vec) delete seg;
		vec.clear();
	}
}

template<> NoteData* HiddenPtrTraits<NoteData>::Copy(const NoteData*) { return nullptr; }
template<> void HiddenPtrTraits<NoteData>::Delete(NoteData*) {}

Steps::Steps(Song* song)
	: m_Timing(0.0f),
	  m_StepsType(StepsType_Invalid),
	  m_StepsTypeStr(""),
	  m_pSong(song),
	  parent(nullptr),
	  m_bNoteDataIsFilled(false),
	  m_bSavedToDisk(false),
	  m_LoadedFromProfile(ProfileSlot_Invalid),
	  m_iHash(0),
	  m_Difficulty(Difficulty_Invalid),
	  m_iMeter(0),
	  m_bAreCachedRadarValuesJustLoaded(false),
	  m_bAreCachedTechCountsValuesJustLoaded(false),
	  m_AreCachedNpsPerMeasureJustLoaded(false),
	  m_AreCachedNotesPerMeasureJustLoaded(false),
	  m_bIsCachedGrooveStatsHashJustLoaded(false),
	  m_iGrooveStatsHashVersion(0),
	  displayBPMType(DISPLAY_BPM_ACTUAL),
	  specifiedBPMMin(0.0f),
	  specifiedBPMMax(0.0f)
{
	for (auto& rv : m_CachedRadarValues) rv.Zero();
	for (auto& tc : m_CachedTechCounts) tc.Zero();
}

Steps::~Steps() = default;
#endif

// ---------------------------------------------------------------------------
// Song stubs sufficient for parsing
Song::Song()
	: m_SelectionDisplay(SHOW_ALWAYS),
	  m_fMusicSampleStartSeconds(0.0f),
	  m_fMusicSampleLengthSeconds(0.0f),
	  m_DisplayBPMType(DISPLAY_BPM_ACTUAL),
	  m_fSpecifiedBPMMin(0.0f),
	  m_fSpecifiedBPMMax(0.0f)
{
	for (auto& vec : m_vpStepsByType) vec.clear();
}
Song::~Song() { DetachSteps(); }
void Song::Reset() { DetachSteps(); }
void Song::DetachSteps() {
	for (auto* steps : m_vpSteps) delete steps;
	m_vpSteps.clear();
	for (auto& vec : m_vpStepsByType) vec.clear();
}
bool Song::LoadFromSongDir(RString, bool, ProfileSlot) { return false; }
bool Song::ReloadFromSongDir(RString) { return false; }
void Song::LoadEditsFromSongDir(RString) {}
bool Song::HasAutosaveFile() { return false; }
bool Song::LoadAutosaveFile() { return false; }
void Song::TidyUpData(bool, bool) {}
void Song::ReCalculateStepStatsAndLastSecond(bool, bool) {}
void Song::TranslateTitles() {}
void Song::AddBackgroundChange(BackgroundLayer, BackgroundChange) {}
void Song::AddForegroundChange(BackgroundChange) {}
bool Song::HasSignificantBpmChangesOrStops() const { return false; }
void Song::GetDisplayBpms(DisplayBpms& bpms) const { bpms.Add(m_fSpecifiedBPMMin); bpms.Add(m_fSpecifiedBPMMax); }
RString Song::GetDisplayMainTitle() const { return m_sMainTitleTranslit.empty() ? m_sMainTitle : m_sMainTitleTranslit; }
RString Song::GetDisplaySubTitle() const { return m_sSubTitleTranslit.empty() ? m_sSubTitle : m_sSubTitleTranslit; }
RString Song::GetDisplayArtist() const { return m_sArtistTranslit.empty() ? m_sArtist : m_sArtistTranslit; }
RString Song::GetMainTitle() const { return m_sMainTitle; }
RString Song::GetSongAssetPath(RString sPath, const RString& sSongPath) {
	if (sPath.empty()) return sPath;
	if (std::filesystem::path(sPath.c_str()).is_absolute()) return sPath;
	return (std::filesystem::path(sSongPath.c_str()) / sPath.c_str()).string().c_str();
}
Steps* Song::CreateSteps() { return new Steps(this); }
void Song::AddSteps(Steps* steps) {
	if (!steps) return;
	m_vpSteps.push_back(steps);
	if (steps->m_StepsType >= 0 && steps->m_StepsType < NUM_StepsType) {
		m_vpStepsByType[steps->m_StepsType].push_back(steps);
	}
}
const std::vector<BackgroundChange>& Song::GetBackgroundChanges(BackgroundLayer) const {
	static std::vector<BackgroundChange> empty;
	return empty;
}
std::vector<BackgroundChange>& Song::GetBackgroundChanges(BackgroundLayer) {
	static std::vector<BackgroundChange> empty;
	return empty;
}
float Song::GetLastBeat() const { return 0.0f; }
RString Song::GetBackgroundPath() const { return ""; }
void Song::SetSpecifiedLastSecond(const float) {}
void Song::SetFirstSecond(const float) {}
void Song::SetLastSecond(const float) {}
int Song::GetNumStepsLoadedFromProfile(ProfileSlot) const { return 0; }
bool Song::IsEditAlreadyLoaded(Steps*) const { return false; }

// ---------------------------------------------------------------------------
// ScreenMessage helpers
#ifndef ITGMANIA_HARNESS_SOURCE
ScreenMessage ScreenMessageHelpers::ToScreenMessage(const RString& name) { return name; }
RString ScreenMessageHelpers::ScreenMessageToString(ScreenMessage sm) { return sm; }
#endif

RString RageColor::NormalizeColorString(RString sColor) { return sColor; }

const std::vector<RString>& ActorUtil::GetTypeExtensionList(FileType) {
	static std::vector<RString> empty;
	return empty;
}

const RString RANDOM_BACKGROUND_FILE = "";
const RString NO_SONG_BG_FILE = "";
const RString SONG_BACKGROUND_FILE = "";
const RString SBE_UpperLeft = "";
const RString SBE_Centered = "";
const RString SBE_StretchNormal = "";
const RString SBE_StretchNoLoop = "";
const RString SBE_StretchRewind = "";
const RString SBT_CrossFade = "";

#ifndef ITGMANIA_HARNESS_SOURCE
void XNodeStringValue::GetValue(RString& out) const { out = m_sValue; }
void XNodeStringValue::GetValue(int& out) const {
	std::stringstream ss;
	ss << m_sValue;
	ss >> out;
	if (ss.fail()) out = 0;
}
void XNodeStringValue::GetValue(float& out) const {
	std::stringstream ss;
	ss << m_sValue;
	ss >> out;
	if (ss.fail()) out = 0.0f;
}
void XNodeStringValue::GetValue(bool& out) const {
	RString lower = m_sValue;
	lower.MakeLower();
	out = (lower == "1" || lower == "true" || lower == "yes" || lower == "on");
}
void XNodeStringValue::GetValue(unsigned& out) const {
	std::stringstream ss;
	ss << m_sValue;
	unsigned long tmp = 0;
	ss >> tmp;
	out = ss.fail() ? 0u : static_cast<unsigned>(tmp);
}
void XNodeStringValue::PushValue(lua_State* L) const { lua_pushstring(L, m_sValue.c_str()); }
void XNodeStringValue::SetValue(const RString& v) { m_sValue = v; }
void XNodeStringValue::SetValue(int v) { m_sValue = std::to_string(v).c_str(); }
void XNodeStringValue::SetValue(float v) {
	std::ostringstream oss;
	oss << v;
	m_sValue = oss.str().c_str();
}
void XNodeStringValue::SetValue(unsigned v) { m_sValue = std::to_string(v).c_str(); }
void XNodeStringValue::SetValueFromStack(lua_State* L) {
	if (!L) return;
	const char* s = lua_tostring(L, -1);
	m_sValue = s ? s : "";
}

namespace BackgroundUtil {
void AddBackgroundChange(std::vector<BackgroundChange>&, BackgroundChange) {}
void SortBackgroundChangesArray(std::vector<BackgroundChange>&) {}
void GetBackgroundEffects(const RString&, std::vector<RString>& paths, std::vector<RString>& names) { paths.clear(); names.clear(); }
void GetBackgroundTransitions(const RString&, std::vector<RString>& paths, std::vector<RString>& names) { paths.clear(); names.clear(); }
void GetSongBGAnimations(const Song*, const RString&, std::vector<RString>& paths, std::vector<RString>& names) { paths.clear(); names.clear(); }
void GetSongMovies(const Song*, const RString&, std::vector<RString>& paths, std::vector<RString>& names) { paths.clear(); names.clear(); }
void GetSongBitmaps(const Song*, const RString&, std::vector<RString>& paths, std::vector<RString>& names) { paths.clear(); names.clear(); }
void GetGlobalBGAnimations(const Song*, const RString&, std::vector<RString>& paths, std::vector<RString>& names) { paths.clear(); names.clear(); }
void GetGlobalRandomMovies(const Song*, const RString&, std::vector<RString>& paths, std::vector<RString>& names, bool, bool) { paths.clear(); names.clear(); }
void BakeAllBackgroundChanges(Song*) {}
}

XNode::XNode() : m_sName("") {}
XNode::XNode(const RString& sName) : m_sName(sName) {}
XNode::XNode(const XNode& cpy) : m_sName(cpy.m_sName) {}
const XNodeValue* XNode::GetAttr(const RString& sAttrName) const {
	auto it = m_attrs.find(sAttrName);
	return it != m_attrs.end() ? it->second : nullptr;
}
XNodeValue* XNode::GetAttr(const RString& sAttrName) {
	auto it = m_attrs.find(sAttrName);
	return it != m_attrs.end() ? it->second : nullptr;
}
bool XNode::PushAttrValue(lua_State*, const RString&) const { return false; }
const XNode* XNode::GetChild(const RString& sName) const {
	for (auto* child : m_childs) {
		if (child && child->GetName() == sName) return child;
	}
	return nullptr;
}
XNode* XNode::GetChild(const RString& sName) { return const_cast<XNode*>(static_cast<const XNode*>(this)->GetChild(sName)); }
bool XNode::PushChildValue(lua_State*, const RString&) const { return false; }
XNode* XNode::AppendChild(XNode* node) {
	if (!node) return nullptr;
	m_childs.push_back(node);
	m_children_by_name.emplace(node->GetName(), node);
	return node;
}
bool XNode::RemoveChild(XNode* node, bool bDelete) {
	if (!node) return false;
	for (auto it = m_childs.begin(); it != m_childs.end(); ++it) {
		if (*it == node) {
			m_children_by_name.erase(node->GetName());
			if (bDelete) delete node;
			m_childs.erase(it);
			return true;
		}
	}
	return false;
}
void XNode::RemoveChildFromByName(XNode* node) {
	if (!node) return;
	m_children_by_name.erase(node->GetName());
}
void XNode::RenameChildInByName(XNode* node) {
	if (!node) return;
	RemoveChildFromByName(node);
	m_children_by_name.emplace(node->GetName(), node);
}
XNodeValue* XNode::AppendAttrFrom(const RString& sName, XNodeValue* value, bool bOverwrite) {
	if (!value) return nullptr;
	auto it = m_attrs.find(sName);
	if (it != m_attrs.end()) {
		if (bOverwrite) {
			delete it->second;
			it->second = value;
		}
		return it->second;
	}
	m_attrs[sName] = value;
	return value;
}
XNodeValue* XNode::AppendAttr(const RString& sName) { return AppendAttrFrom(sName, new XNodeStringValue, true); }
bool XNode::RemoveAttr(const RString& sName) {
	auto it = m_attrs.find(sName);
	if (it == m_attrs.end()) return false;
	delete it->second;
	m_attrs.erase(it);
	return true;
}
void XNode::Clear() { Free(); }
void XNode::Free() {
	for (auto* child : m_childs) delete child;
	m_childs.clear();
	m_children_by_name.clear();
	for (auto& kv : m_attrs) delete kv.second;
	m_attrs.clear();
}
#endif

IniFile::IniFile() = default;
bool IniFile::ReadFile(const RString&) { return false; }

namespace DWILoader {
void GetApplicableFiles(const RString&, std::vector<RString>& out) { out.clear(); }
bool LoadFromDir(const RString&, Song&, std::set<RString>&) { return false; }
bool LoadNoteDataFromSimfile(const RString&, Steps&) { return false; }
}
namespace KSFLoader {
void GetApplicableFiles(const RString&, std::vector<RString>& out) { out.clear(); }
bool LoadFromDir(const RString&, Song&) { return false; }
bool LoadNoteDataFromSimfile(const RString&, Steps&) { return false; }
}
namespace BMSLoader {
void GetApplicableFiles(const RString&, std::vector<RString>& out) { out.clear(); }
bool LoadFromDir(const RString&, Song&) { return false; }
bool LoadNoteDataFromSimfile(const RString&, Steps&) { return false; }
}

#ifndef ITGMANIA_HARNESS_SOURCE
namespace StringConversion {
template<> bool FromString<float>(const RString& sValue, float& out) {
	std::stringstream ss;
	ss << sValue;
	ss >> out;
	return !ss.fail() && ss.eof();
}
template<> bool FromString<bool>(const RString& sValue, bool& out) {
	RString lower = sValue;
	lower.MakeLower();
	if (lower == "1" || lower == "true" || lower == "yes" || lower == "on") { out = true; return true; }
	if (lower == "0" || lower == "false" || lower == "no" || lower == "off") { out = false; return true; }
	return false;
}
template<> RString ToString<float>(const float& value) {
	std::ostringstream oss;
	oss << value;
	return oss.str().c_str();
}
template<> RString ToString<bool>(const bool& value) { return value ? "true" : "false"; }
} // namespace StringConversion
#endif

// ---------------------------------------------------------------------------
// Simple initialization helper to create globals
static void init_globals_once() {
	static bool done = false;
	if (done) return;

	// Construct only the few preferences we need by hand.
	static std::aligned_storage_t<sizeof(PrefsManager), alignof(PrefsManager)> prefs_storage;
	PrefsManager* prefs_raw = reinterpret_cast<PrefsManager*>(&prefs_storage);
	std::memset(prefs_raw, 0, sizeof(PrefsManager));
	new (&prefs_raw->m_fGlobalOffsetSeconds) Preference<float>("GlobalOffsetSeconds", 0.0f);
	new (&prefs_raw->m_bQuirksMode) Preference<bool>("QuirksMode", false);
	new (&prefs_raw->m_bLightsSimplifyBass) Preference<bool>("LightsSimplifyBass", false);
	PREFSMAN = prefs_raw;

	done = true;
}

struct GlobalInit {
	GlobalInit() { init_globals_once(); }
} _globalInit;

#ifndef ITGMANIA_HARNESS_SOURCE
void init_itgmania_runtime(int, char**) { init_globals_once(); }
#endif

int luaL_pushtype(lua_State* L, int n) {
	const char* t = lua_typename(L, lua_type(L, n));
	lua_pushstring(L, t ? t : "");
	return 1;
}
