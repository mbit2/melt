// license:BSD-3-Clause
// copyright-holders:Aaron Giles, Vas Crabb
/***************************************************************************

    un7z.c

    Functions to manipulate data within 7z files.

***************************************************************************/

// this is based on unzip.c, with modifications needed to use the 7zip library

#include "unzip.h"

#include "corestr.h"
#include "unicode.h"

#include "lzma/C/7z.h"
#include "lzma/C/7zCrc.h"
#include "lzma/C/7zVersion.h"

#include <algorithm>
#include <array>
#include <cassert>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <mutex>
#include <utility>
#include <vector>


namespace util {
namespace {
/***************************************************************************
    TYPE DEFINITIONS
***************************************************************************/

struct CSzFile
{
	CSzFile() : currfpos(0), length(0), osdfile() {}

	long            currfpos;
	std::uint64_t   length;
	osd_file::ptr   osdfile;

	WRes read(void *data, std::size_t &size)
	{
		if (!osdfile)
		{
			std::printf("un7z.c: called File_Read without file\n");
			return 1;
		}

		if (!size) return 0;
		size_t originalSize = size;

		std::uint32_t read_length;
		//osd_file::error err =
		osdfile->read(data, currfpos, originalSize, read_length);
		size = read_length;
		currfpos += read_length;

		if (size == originalSize)
			return 0;

		return 0;
	}

	WRes seek(Int64 &pos, ESzSeek origin)
	{
		if (origin == 0) currfpos = pos;
		if (origin == 1) currfpos = currfpos + pos;
		if (origin == 2) currfpos = length -pos;

		pos = currfpos;
		return 0;
	}
};


struct CFileInStream : public ISeekInStream, public CSzFile
{
	CFileInStream();
};


class  m7z_file_impl
{
public:
	typedef std::unique_ptr<m7z_file_impl> ptr;

	m7z_file_impl(const std::string &filename);
	~m7z_file_impl()
	{
		if (m_out_buffer)
			IAlloc_Free(&m_alloc_imp, m_out_buffer);
		if (m_inited)
			SzArEx_Free(&m_db, &m_alloc_imp);
	}

	static ptr find_cached(const std::string &filename)
	{
		std::lock_guard<std::mutex> guard(s_cache_mutex);
		for (std::size_t cachenum = 0; cachenum < s_cache.size(); cachenum++)
		{
			// if we have a valid entry and it matches our filename, use it and remove from the cache
			if (s_cache[cachenum] && (filename == s_cache[cachenum]->m_filename))
			{
				ptr result;
				std::swap(s_cache[cachenum], result);
				return result;
			}
		}
		return ptr();
	}
	static void close(ptr &&archive);
	static void cache_clear()
	{
		// clear call cache entries
		std::lock_guard<std::mutex> guard(s_cache_mutex);
		for (std::size_t cachenum = 0; cachenum < s_cache.size(); s_cache[cachenum++].reset()) { }
	}

	archive_file::error initialize();

	int first_file() { return search(0, 0, std::string(), false, false, false); }
	int next_file() { return (m_curr_file_idx < 0) ? -1 : search(m_curr_file_idx + 1, 0, std::string(), false, false, false); }

	int search(std::uint32_t crc)
	{
		return search(0, crc, std::string(), true, false, false);
	}
	int search(const std::string &filename, bool partialpath)
	{
		return search(0, 0, filename, false, true, partialpath);
	}
	int search(std::uint32_t crc, const std::string &filename, bool partialpath)
	{
		return search(0, crc, filename, true, true, partialpath);
	}

	bool current_is_directory() const { return m_curr_is_dir; }
	const std::string &current_name() const { return m_curr_name; }
	std::uint64_t current_uncompressed_length() const { return m_curr_length; }
	std::uint32_t current_crc() const { return m_curr_crc; }

	archive_file::error decompress(void *buffer, std::uint32_t length);

private:
	m7z_file_impl(const m7z_file_impl &) = delete;
	m7z_file_impl(m7z_file_impl &&) = delete;
	m7z_file_impl &operator=(const m7z_file_impl &) = delete;
	m7z_file_impl &operator=(m7z_file_impl &&) = delete;

	int search(
			int i,
			std::uint32_t search_crc,
			const std::string &search_filename,
			bool matchcrc,
			bool matchname,
			bool partialpath);
	void make_utf8_name(int index);

	static constexpr std::size_t        CACHE_SIZE = 8;
	static std::array<ptr, CACHE_SIZE>  s_cache;
	static std::mutex                   s_cache_mutex;

	const std::string           m_filename;             // copy of _7Z filename (for caching)

	int                         m_curr_file_idx;        // current file index
	bool                        m_curr_is_dir;          // current file is directory
	std::string                 m_curr_name;            // current file name
	std::uint64_t               m_curr_length;          // current file uncompressed length
	std::uint32_t               m_curr_crc;             // current file crc

	std::vector<UInt16>         m_utf16_buf;
	std::vector<unicode_char>   m_uchar_buf;
	std::vector<char>           m_utf8_buf;

	CFileInStream               m_archive_stream;
	CLookToRead                 m_look_stream;
	CSzArEx                     m_db;
	ISzAlloc                    m_alloc_imp;
	ISzAlloc                    m_alloc_temp_imp;
	bool                        m_inited;

	// cached stuff for solid blocks
	UInt32                      m_block_index;
	Byte *                      m_out_buffer;
	std::size_t                 m_out_buffer_size;

};


class m7z_file_wrapper : public archive_file
{
public:
	m7z_file_wrapper(m7z_file_impl::ptr &&impl) : m_impl(std::move(impl)) { assert(m_impl); }
	virtual ~m7z_file_wrapper() override { m7z_file_impl::close(std::move(m_impl)); }

	virtual int first_file() override { return m_impl->first_file(); }
	virtual int next_file() override { return m_impl->next_file(); }

	virtual int search(std::uint32_t crc) override
	{
		return m_impl->search(crc);
	}
	virtual int search(const std::string &filename, bool partialpath) override
	{
		return m_impl->search(filename, partialpath);
	}
	virtual int search(std::uint32_t crc, const std::string &filename, bool partialpath) override
	{
		return m_impl->search(crc, filename, partialpath);
	}

	virtual bool current_is_directory() const override { return m_impl->current_is_directory(); }
	virtual const std::string &current_name() const override { return m_impl->current_name(); }
	virtual std::uint64_t current_uncompressed_length() const override { return m_impl->current_uncompressed_length(); }
	virtual std::uint32_t current_crc() const override { return m_impl->current_crc(); }

	virtual error decompress(void *buffer, std::uint32_t length) override { return m_impl->decompress(buffer, length); }

private:
	m7z_file_impl::ptr m_impl;
};



/***************************************************************************
    GLOBAL VARIABLES
***************************************************************************/

std::array<m7z_file_impl::ptr, m7z_file_impl::CACHE_SIZE> m7z_file_impl::s_cache;
std::mutex m7z_file_impl::s_cache_mutex;



/***************************************************************************
    7Zip Memory / File handling (adapted from 7zfile.c/.h and 7zalloc.c/.h)
***************************************************************************/

/* ---------- FileInStream ---------- */

extern "C" {
static void *SZipAlloc(void *p, std::size_t size)
{
	return (size == 0) ? nullptr : std::malloc(size);
}

static void SZipFree(void *p, void *address)
{
	std::free(address);
}


static SRes FileInStream_Read(void *pp, void *buf, size_t *size)
{
	return (reinterpret_cast<CFileInStream *>(pp)->read(buf, *size) == 0) ? SZ_OK : SZ_ERROR_READ;
}

static SRes FileInStream_Seek(void *pp, Int64 *pos, ESzSeek origin)
{
	return reinterpret_cast<CFileInStream *>(pp)->seek(*pos, origin);
}

} // extern "C"


CFileInStream::CFileInStream()
{
	Read = &FileInStream_Read;
	Seek = &FileInStream_Seek;
}



/***************************************************************************
    CACHE MANAGEMENT
***************************************************************************/

m7z_file_impl::m7z_file_impl(const std::string &filename)
	: m_filename(filename)
	, m_curr_file_idx(-1)
	, m_curr_is_dir(false)
	, m_curr_name()
	, m_curr_length(0)
	, m_curr_crc(0)
	, m_utf16_buf(128)
	, m_uchar_buf(128)
	, m_utf8_buf(512)
	, m_inited(false)
	, m_block_index(0xffffffff) // it can have any value before first call (if outBuffer = 0)
	, m_out_buffer(nullptr)     // it must be 0 before first call for each new archive
	, m_out_buffer_size(0)      // it can have any value before first call (if outBuffer = 0)
{
	m_alloc_imp.Alloc = SZipAlloc;
	m_alloc_imp.Free = SZipFree;

	m_alloc_temp_imp.Alloc = SZipAlloc;
	m_alloc_temp_imp.Free = SZipFree;
}


archive_file::error m7z_file_impl::initialize()
{
	osd_file::error const err = osd_file::open(m_filename, OPEN_FLAG_READ, m_archive_stream.osdfile, m_archive_stream.length);
	if (err != osd_file::error::NONE)
		return archive_file::error::FILE_ERROR;

	LookToRead_CreateVTable(&m_look_stream, False);
	m_look_stream.realStream = &m_archive_stream;
	LookToRead_Init(&m_look_stream);

	CrcGenerateTable();

	SzArEx_Init(&m_db);
	m_inited = true;

	SRes const res = SzArEx_Open(&m_db, &m_look_stream.s, &m_alloc_imp, &m_alloc_temp_imp);
	if (res != SZ_OK)
		return archive_file::error::FILE_ERROR;

	return archive_file::error::NONE;
}


/*-------------------------------------------------
    _7z_file_close - close a _7Z file and add it
    to the cache
-------------------------------------------------*/

void m7z_file_impl::close(ptr &&archive)
{
	if (!archive) return;

	// close the open files
	archive->m_archive_stream.osdfile.reset();

	// find the first NULL entry in the cache
	std::lock_guard<std::mutex> guard(s_cache_mutex);
	std::size_t cachenum;
	for (cachenum = 0; cachenum < s_cache.size(); cachenum++)
		if (!s_cache[cachenum])
			break;

	// if no room left in the cache, free the bottommost entry
	if (cachenum == s_cache.size())
		s_cache[--cachenum].reset();

	// move everyone else down and place us at the top
	for ( ; cachenum > 0; cachenum--)
		s_cache[cachenum] = std::move(s_cache[cachenum - 1]);
	s_cache[0] = std::move(archive);
}



/***************************************************************************
    7Z FILE ACCESS
***************************************************************************/

/*-------------------------------------------------
    _7z_file_decompress - decompress a file
    from a _7Z into the target buffer
-------------------------------------------------*/

archive_file::error m7z_file_impl::decompress(void *buffer, std::uint32_t length)
{
	// make sure the file is open..
	if (!m_archive_stream.osdfile)
	{
		m_archive_stream.currfpos = 0;
		osd_file::error const err = osd_file::open(m_filename, OPEN_FLAG_READ, m_archive_stream.osdfile, m_archive_stream.length);
		if (err != osd_file::error::NONE)
			return archive_file::error::FILE_ERROR;
	}

	size_t offset = 0;
	size_t out_size_processed = 0;

	SRes const res = SzArEx_Extract(
			&m_db, &m_look_stream.s, m_curr_file_idx,
			&m_block_index,
			&m_out_buffer, &m_out_buffer_size,
			&offset, &out_size_processed,
			&m_alloc_imp, &m_alloc_temp_imp);

	if (res != SZ_OK)
		return archive_file::error::FILE_ERROR;

	std::memcpy(buffer, m_out_buffer + offset, length);

	return archive_file::error::NONE;
}


int m7z_file_impl::search(
		int i,
		std::uint32_t search_crc,
		const std::string &search_filename,
		bool matchcrc,
		bool matchname,
		bool partialpath)
{
	for ( ; i < m_db.db.NumFiles; i++)
	{
		const CSzFileItem &f(m_db.db.Files[i]);

		make_utf8_name(i);
		const std::uint64_t size(f.Size);
		const std::uint32_t crc(f.Crc);
		const bool crcmatch(crc == search_crc);
		auto const partialoffset(m_utf8_buf.size() - 1 - search_filename.length());
		bool const partialpossible((m_utf8_buf.size() > (search_filename.length() + 1)) && (m_utf8_buf[partialoffset - 1] == '/'));
		const bool namematch(
				!core_stricmp(search_filename.c_str(), &m_utf8_buf[0]) ||
				(partialpath && partialpossible && !core_stricmp(search_filename.c_str(), &m_utf8_buf[partialoffset])));

		const bool found = ((!matchcrc && !matchname) || !f.IsDir) && (!matchcrc || crcmatch) && (!matchname || namematch);
		if (found)
		{
			m_curr_file_idx = i;
			m_curr_is_dir = bool(f.IsDir);
			m_curr_name = &m_utf8_buf[0];
			m_curr_length = size;
			m_curr_crc = crc;

			return i;
		}
	}

	return -1;
}


void m7z_file_impl::make_utf8_name(int index)
{
	std::size_t len, out_pos;

	len = SzArEx_GetFileNameUtf16(&m_db, index, nullptr);
	m_utf16_buf.resize((std::max<std::size_t>)(m_utf16_buf.size(), len));
	SzArEx_GetFileNameUtf16(&m_db, index, &m_utf16_buf[0]);

	m_uchar_buf.resize((std::max<std::size_t>)(m_uchar_buf.size(), len));
	out_pos = 0;
	for (std::size_t in_pos = 0; in_pos < (len - 1); )
	{
		const int used = uchar_from_utf16(&m_uchar_buf[out_pos], &m_utf16_buf[in_pos], len - in_pos);
		if (used < 0)
		{
			in_pos++;
			m_uchar_buf[out_pos++] = 0x00fffd; // Unicode REPLACEMENT CHARACTER
		}
		else
		{
			assert(used > 0);
			in_pos += used;
			out_pos++;
		}
	}
	len = out_pos;

	m_utf8_buf.resize((std::max<std::size_t>)(m_utf8_buf.size(), 4 * len + 1));
	out_pos = 0;
	for (std::size_t in_pos = 0; in_pos < len; in_pos++)
	{
		int produced = utf8_from_uchar(&m_utf8_buf[out_pos], m_utf8_buf.size() - out_pos, m_uchar_buf[in_pos]);
		if (produced < 0)
			produced = utf8_from_uchar(&m_utf8_buf[out_pos], m_utf8_buf.size() - out_pos, 0x00fffd);
		if (produced >= 0)
			out_pos += produced;
		assert(out_pos < m_utf8_buf.size());
	}
	m_utf8_buf[out_pos++] = '\0';
	m_utf8_buf.resize(out_pos);
}

} // anonymous namespace


archive_file::error archive_file::open_7z(const std::string &filename, ptr &result)
{
	// ensure we start with a NULL result
	result.reset();

	// see if we are in the cache, and reopen if so
	m7z_file_impl::ptr newimpl(m7z_file_impl::find_cached(filename));

	if (!newimpl)
	{
		// allocate memory for the 7z file structure
		try { newimpl = std::make_unique<m7z_file_impl>(filename); }
		catch (...) { return error::OUT_OF_MEMORY; }
		error const err = newimpl->initialize();
		if (err != error::NONE) return err;
	}

	try
	{
		result = std::make_unique<m7z_file_wrapper>(std::move(newimpl));
		return error::NONE;
	}
	catch (...)
	{
		m7z_file_impl::close(std::move(newimpl));
		return error::OUT_OF_MEMORY;
	}
}


/*-------------------------------------------------
    _7z_file_cache_clear - clear the _7Z file
    cache and free all memory
-------------------------------------------------*/

void m7z_file_cache_clear()
{
	// This is a trampoline called from unzip.cpp to avoid the need to have the zip and 7zip code in one file
	m7z_file_impl::cache_clear();
}

} // namespace util
