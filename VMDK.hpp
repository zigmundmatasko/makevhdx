#pragma once
#include "Image.hpp"
#include "RAW.hpp"
#include <PathCch.h>
#include <ctime>

struct VMDK : RAW
{
protected:
	HANDLE image_file_desc;
	WCHAR flat_file_name[MAX_PATH];
	UINT32 sector_size;
public:
	VMDK() = default;
	VMDK(_In_z_ PCWSTR file_name) : RAW(file_name) {
		
		// create new FLAT file name
		wcscpy_s(flat_file_name, file_name);
		PathCchRemoveExtension(flat_file_name, MAX_PATH);
		wcscat_s(flat_file_name, L"-flat.vmdk");
		
	};
	void ReadHeader()
	{
		RAW::ReadHeader();
	}
	void ConstructHeader(_In_ UINT64 disk_size, _In_ UINT32 block_size, _In_ UINT32 sector_size_in, _In_ bool is_fixed)
	{
		sector_size = sector_size_in;
		RAW::ConstructHeader(disk_size, block_size, sector_size, is_fixed);
	}
	void WriteHeader() const
	{
		// Open descriptor file = original_file_name
		FILE* f;
		_wfopen_s(&f,original_file_name, L"wt");

		// write metadata
		/*
			# Disk DescriptorFile
			version = 1
			CID = RANDOM UINT32
			parentCID = ffffffff
			createType = "VMFS"
			isNativeSnapshot = "no"

			# Extent description
			RW COUNT_OF_SECTORS VMFS "FILENAME-flat.vmdk"

			# The Disk Data Base
			ddb.virtualHWVersion = "7"
			ddb.adapterType = "lsilogic"
			ddb.geometry.sectors = "255"
			ddb.geometry.heads = "16"
			ddb.geometry.cylinders = "COUNT_OF_SECTORS/16/255"
		*/

		if (f) {
			srand((unsigned)time(0));
			fwprintf(
				f,
				L"# Disk DescriptorFile\n"
				L"version = 1\n"
				L"CID = %8.8x\n"
				L"parentCID = ffffffff\n"
				L"createType = \"VMFS\"\n"
				L"isNativeSnapshot = \"no\"\n"
				L"\n"
				L"RW %llu VMFS \"%s\"\n"
				L"\n"
				L"ddb.virtualHWVersion = \"7\"\n"
				L"ddb.adapterType = \"lsilogic\"\n"
				L"ddb.geometry.sectors = \"255\"\n"
				L"ddb.geometry.heads = \"16\"\n"
				L"ddb.geometry.cylinders = \"%lli\"\n",
				rand(),
				raw_disk_size.QuadPart / sector_size,
				PathFindFileNameW(flat_file_name),
				raw_disk_size.QuadPart / sector_size / 16 / 255
			);
			// Close descriptor file
			fclose(f);
		}

		RAW::WriteHeader();
	}
	PCSTR GetImageTypeName() const noexcept
	{
		return "VMDK";
	}
	PCWSTR GetFileName() {
		return flat_file_name;
	}
};