/*
 * IAsyncFile.h
 *
 * This source file is part of the FoundationDB open source project
 *
 * Copyright 2013-2018 Apple Inc. and the FoundationDB project authors
 * 
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 * 
 *     http://www.apache.org/licenses/LICENSE-2.0
 * 
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#ifndef FLOW_IASYNCFILE_H
#define FLOW_IASYNCFILE_H
#pragma once

#include "flow/flow.h"

//All outstanding operations must be cancelled before the destructor of IAsyncFile is called.
class IAsyncFile {
public:
	virtual ~IAsyncFile();
	// Pass these to g_network->open to get an IAsyncFile
	enum {
		// Implementation relies on the low bits being the same as the SQLite flags (this is validated by a static_assert there)
		OPEN_READONLY = 0x1, 
		OPEN_READWRITE = 0x2, 
		OPEN_CREATE = 0x4, 
		OPEN_EXCLUSIVE = 0x10, 
		
		// Further flag values are arbitrary bits
		OPEN_UNBUFFERED = 0x10000, 
		OPEN_UNCACHED = 0x20000, 
		OPEN_LOCK = 0x40000, 
		OPEN_ATOMIC_WRITE_AND_CREATE = 0x80000,  // A temporary file is opened, and on the first call to sync() it is atomically renamed to the given filename
		OPEN_LARGE_PAGES = 0x100000, 
		OPEN_NO_AIO = 0x200000,                   // Don't use AsyncFileKAIO or similar implementations that rely on filesystem support for AIO
		OPEN_CACHED_READ_ONLY = 0x400000          // AsyncFileCached opens files read/write even if you specify read only
	};  

	virtual void addref() = 0;
	virtual void delref() = 0;

	// For read() and write(), the data buffer must remain valid until the future is ready
	virtual Future<int> read( void* data, int length, int64_t offset ) = 0;  // Returns number of bytes actually read (from [0,length])
	virtual Future<Void> write( void const* data, int length, int64_t offset ) = 0;
	// The zeroed data is not guaranteed to be durable after `zeroRange` returns.  A call to sync() would be required.
	virtual Future<Void> zeroRange( int64_t offset, int64_t length );
	virtual Future<Void> truncate( int64_t size ) = 0;
	virtual Future<Void> sync() = 0;
	virtual Future<Void> flush() { return Void();  }      // Sends previous writes to the OS if they have been buffered in memory, but does not make them power safe
	virtual Future<int64_t> size() = 0;
	virtual std::string getFilename() = 0;

	// Attempt to read the *length bytes at offset without copying.  If successful, a pointer to the
	//   requested bytes is written to *data, and the number of bytes successfully read is
	//   written to *length.  If unsuccessful, *data and *length are undefined.
	// readZeroCopy may fail (returning io_error) at any time, even if the requested bytes are readable.
	//   For example, an implementation of IAsyncFile may not implement readZeroCopy or may implement it
	//   only in certain cases (e.g. when the requested range does not cross a page boundary).  So callers
	//   should always retry a failed readZeroCopy as a read().
	// Once readZeroCopy succeeds, the returned bytes will be pinned in memory until releaseZeroCopy is
	//   called, so the caller must always ensure that a matching call to releaseZeroCopy takes place.
	// Between readZeroCopy and releaseZeroCopy, it is illegal (undefined behavior) to concurrently write
	//   to an overlapping range of bytes, whether or not using the same IAsyncFile handle.
	virtual Future<Void> readZeroCopy( void** data, int* length, int64_t offset ) { return io_error(); }
	virtual void releaseZeroCopy( void* data, int length, int64_t offset ) {}

	virtual int64_t debugFD() = 0;
};

typedef void (*runCycleFuncPtr)();

class IAsyncFileSystem {
public:
	// Opens a file for asynchronous I/O
	virtual Future< Reference<class IAsyncFile> > open( std::string filename, int64_t flags, int64_t mode ) = 0;

	// Deletes the given file.  If mustBeDurable, returns only when the file is guaranteed to be deleted even after a power failure.
	virtual Future< Void > deleteFile( std::string filename, bool mustBeDurable ) = 0;

	// Unlinks a file and then deletes it slowly by truncating the file repeatedly.
	// If mustBeDurable, returns only when the file is guaranteed to be deleted even after a power failure.
	virtual Future<Void> incrementalDeleteFile( std::string filename, bool mustBeDurable );

	static IAsyncFileSystem* filesystem() { return filesystem(g_network); }
	static runCycleFuncPtr runCycleFunc() { return reinterpret_cast<runCycleFuncPtr>(reinterpret_cast<flowGlobalType>(g_network->global(INetwork::enRunCycleFunc))); }

	static IAsyncFileSystem* filesystem(INetwork* networkPtr) { return static_cast<IAsyncFileSystem*>(networkPtr->global(INetwork::enFileSystem)); }


protected:
	IAsyncFileSystem() {}
	virtual ~IAsyncFileSystem() {}	// Please don't try to delete through this interface!
};

#endif
