#pragma once
#define WIN32_LEAN_AND_MEAN
#define STRICT_GS_ENABLED
#define _ATL_NO_AUTOMATIC_NAMESPACE
#include <atlbase.h>
#include <windows.h>
#include <algorithm>
#include <memory>
#include <optional>
#include <winioctl.h>
#include "miscutil.hpp"

const UINT32 DEFAULT_SECTOR_SIZE = 512;
class Image
{
protected:
	HANDLE image_file;
	UINT32 require_alignment;
	BY_HANDLE_FILE_INFORMATION file_info;
	FSCTL_GET_INTEGRITY_INFORMATION_BUFFER image_integrity;
	PCWSTR original_file_name;
	
	Image() = default;
	Image(_In_z_ PCWSTR file_name) {
		original_file_name = file_name;
	}
	Image(_In_ HANDLE file, _In_ UINT32 cluster_size) : image_file(file), require_alignment(cluster_size)
	{
		if (!IsPow2(require_alignment))
		{
			die(L"Require alignment is not power of 2.");
		}
	}
public:
	Image(const Image&) = delete;
	Image& operator=(const Image&) = delete;
	virtual ~Image() = default;

	virtual void Attach(_In_ HANDLE file, _In_ UINT32 cluster_size)
	{
		if (!IsPow2(cluster_size))
		{
			die(L"Require alignment is not power of 2.");
		}
		image_file = file;
		require_alignment = cluster_size;
		ATLENSURE(GetFileInformationByHandle(file, &file_info));
	}
	virtual void Attach(_In_ HANDLE file, _In_ FSCTL_GET_INTEGRITY_INFORMATION_BUFFER integrity) {
		image_integrity = integrity;
		Attach(file, integrity.ClusterSizeInBytes);
	}
	virtual void ReadHeader() = 0;
	virtual void ConstructHeader(_In_ UINT64 disk_size, _In_ UINT32 block_size, _In_ UINT32 sector_size, _In_ bool is_fixed) = 0;
	virtual void WriteHeader() const = 0;
	virtual bool CheckConvertible(_When_(return == false, _Outptr_result_z_) PCWSTR* reason) const = 0;
	virtual bool IsFixed() const noexcept = 0;
	virtual PCSTR GetImageTypeName() const noexcept = 0;
	virtual UINT64 GetDiskSize() const noexcept = 0;
	virtual UINT32 GetSectorSize() const noexcept
	{
		return DEFAULT_SECTOR_SIZE;
	}
	virtual UINT32 GetBlockSize() const noexcept = 0;
	virtual UINT32 GetTableEntriesCount() const noexcept = 0;
	virtual std::optional<UINT64> ProbeBlock(_In_ UINT32 index) const noexcept = 0;
	virtual UINT64 AllocateBlockForWrite(_In_ UINT32 index) = 0;
	virtual FSCTL_GET_INTEGRITY_INFORMATION_BUFFER GetIntegrity() {
		return image_integrity;
	};
	virtual BY_HANDLE_FILE_INFORMATION GetFileInfo() {
		return file_info;
	}
	virtual HANDLE GetFileH() {
		return image_file;
	};
	virtual PCWSTR GetFileName() {
		return original_file_name;
	}
};