#include "ConvertImage.hpp"
#include "VHD.hpp"
#include "VHDX.hpp"
#include "RAW.hpp"
#include "VMDK.hpp"
#include <typeinfo>

Image *DetectImageFormatByData(_In_ HANDLE file, _In_ boolean raw)
{
	if (!raw) {
		decltype(VHDX_FILE_IDENTIFIER::Signature) vhdx_signature;
		ReadFileWithOffset(file, &vhdx_signature, VHDX_FILE_IDENTIFIER_OFFSET);
		if (vhdx_signature == VHDX_SIGNATURE)
		{
			return (new VHDX);
		}
		LARGE_INTEGER fsize;
		ATLENSURE(GetFileSizeEx(file, &fsize));
		decltype(VHD_FOOTER::Cookie) vhd_cookie;
		ReadFileWithOffset(file, &vhd_cookie, ROUNDUP(fsize.QuadPart - VHD_DYNAMIC_HEADER_OFFSET, sizeof(VHD_FOOTER)));
		if (vhd_cookie == VHD_COOKIE)
		{
			return (new VHD);
		}
		if (fsize.LowPart % RAW_SECTOR_SIZE != 0)
		{
			die(L"Image type detection failed.");
		}
	}
	return (new RAW);
}
Image *DetectImageFormatByExtension(_In_z_ PCWSTR file_name, _In_ boolean raw)
{
	if (!raw) {
		PCWSTR extension = PathFindExtensionW(file_name);
		if (_wcsicmp(extension, L".vhdx") == 0)
		{
			return (new VHDX(file_name));
		}
		if (_wcsicmp(extension, L".vhd") == 0)
		{
			return (new VHD(file_name));
		}
		if (_wcsicmp(extension, L".vmdk") == 0)
		{
			return (new VMDK(file_name));
		}
		if (_wcsicmp(extension, L".avhdx") == 0 || _wcsicmp(extension, L".avhd") == 0)
		{
			die(L".avhdx/.avhd is not allowed.");
		}
	}
	return (new RAW(file_name));
}

Image *OpenSrc(_In_z_ PCWSTR file_name, _In_ boolean raw) {
	HANDLE src_file = CreateFileW(file_name, GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING, FILE_FLAG_SEQUENTIAL_SCAN, nullptr);
	if (src_file == INVALID_HANDLE_VALUE)
	{
		//src_file.Detach();
		die();
	}
	ULONG fs_flags;
	ATLENSURE(GetVolumeInformationByHandleW(src_file, nullptr, 0, nullptr, nullptr, &fs_flags, nullptr, 0));
	if (!(fs_flags & FILE_SUPPORTS_BLOCK_REFCOUNTING))
	{
		die(L"Filesystem doesn't support Block Cloning feature.");
	}
	ULONG junk;
	FSCTL_GET_INTEGRITY_INFORMATION_BUFFER get_integrity;
	if (!DeviceIoControl(src_file, FSCTL_GET_INTEGRITY_INFORMATION, nullptr, 0, &get_integrity, sizeof get_integrity, &junk, nullptr))
	{
		die();
	}
	Image *img = DetectImageFormatByData(src_file, raw);
	img->Attach(src_file, get_integrity);
	img->ReadHeader();
	if (PCWSTR reason; !img->CheckConvertible(&reason))
	{
		die(reason);
	}
	return (img);
}

Image *OpenDst(_In_z_ PCWSTR file_name, _In_ bool force_sparse, _In_ UINT32 block_size, _In_ bool is_fixed, _In_ boolean raw, Image *src_img) {

	Image *img = DetectImageFormatByExtension(file_name, raw);
	if( _stricmp(img->GetImageTypeName(), "VMDK") == 0 ) force_sparse = TRUE;
	
	HANDLE dst_file = CreateFileW(img->GetFileName(), GENERIC_READ | GENERIC_WRITE | DELETE, 0, nullptr, CREATE_ALWAYS, FILE_FLAG_SEQUENTIAL_SCAN, nullptr);
	if (dst_file == INVALID_HANDLE_VALUE)
	{
		//dst_file.Detach();
		die();
	}

	FILE_DISPOSITION_INFO dispos = { TRUE };
	ATLENSURE(SetFileInformationByHandle(dst_file, FileDispositionInfo, &dispos, sizeof dispos));
	FSCTL_SET_INTEGRITY_INFORMATION_BUFFER set_integrity = { src_img->GetIntegrity().ChecksumAlgorithm, 0, src_img->GetIntegrity().Flags };
	if (!DeviceIoControl(dst_file, FSCTL_SET_INTEGRITY_INFORMATION, &set_integrity, sizeof set_integrity, nullptr, 0, nullptr, nullptr))
	{
		die();
	}

	if (force_sparse || src_img->GetFileInfo().dwFileAttributes & FILE_ATTRIBUTE_SPARSE_FILE)
	{	
		ULONG junk;
		if (!DeviceIoControl(dst_file, FSCTL_SET_SPARSE, nullptr, 0, nullptr, 0, &junk, nullptr))
		{
			die();
		}
	}
	img->Attach(dst_file, src_img->GetIntegrity().ClusterSizeInBytes);
	img->ConstructHeader(src_img->GetDiskSize(), block_size, src_img->GetSectorSize(), is_fixed);

	return (img);
}

void ConvertImage(_In_z_ PCWSTR src_file_name, _In_z_ PCWSTR dst_file_name, _In_ const Option& options)
{
	ULONG junk;

	// source
	wprintf(
		L"Source\n"
		L"Path:              %ls\n",
		src_file_name
	);
	Image *src_img = OpenSrc(src_file_name, options.raw);
	//DWORD ClusterSizeInBytes = src_img->GetIntegrity().ClusterSizeInBytes;

	wprintf(
		L"Image format:      %hs\n"
		L"Allocation policy: %hs\n"
		L"Disk size:         %llu(%.3fGB)\n"
		L"Block size:        %.1fMB\n",
		src_img->GetImageTypeName(),
		src_img->IsFixed() ? "Preallocate" : "Dynamic",
		src_img->GetDiskSize(),
		src_img->GetDiskSize() / (1024.f * 1024.f * 1024.f),
		src_img->GetBlockSize() / (1024.f * 1024.f)
	);

	// destination
	wprintf(
		L"\n"
		L"Destination\n"
		L"Path Requested     %ls\n",
		dst_file_name
	);
	bool is_sparse = options.force_sparse || src_img->GetFileInfo().dwFileAttributes & FILE_ATTRIBUTE_SPARSE_FILE;
	bool is_fixed = options.is_fixed.value_or(src_img->IsFixed());

	UINT32 block_size = options.block_size ? options.block_size : src_img->GetBlockSize();
	Image *dst_img = OpenDst(dst_file_name, is_sparse, block_size, is_fixed, options.raw, src_img);

	wprintf(
		L"Path Modify        %ls\n"
		L"Image format:      %hs\n"
		L"Allocation policy: %hs\n"
		L"Disk size:         %llu(%.3fGB)\n"
		L"Block size:        %.1fMB\n",
		dst_img->GetFileName(),
		dst_img->GetImageTypeName(),
		dst_img->IsFixed() ? "Preallocate" : "Dynamic",
		dst_img->GetDiskSize(),
		dst_img->GetDiskSize() / (1024.f * 1024.f * 1024.f),
		dst_img->GetBlockSize() / (1024.f * 1024.f)
	);


	// FOR
	const UINT32 source_block_size = src_img->GetBlockSize();
	const UINT32 destination_block_size = dst_img->GetBlockSize();
	DUPLICATE_EXTENTS_DATA dup_extent = { src_img->GetFileH() };
	if (source_block_size <= destination_block_size)
	{
		for (UINT32 read_block_number = 0; read_block_number < src_img->GetTableEntriesCount(); read_block_number++)
		{
			if (const std::optional<UINT64> read_physical_address = src_img->ProbeBlock(read_block_number))
			{
				const UINT64 read_virtual_address = 1ULL * source_block_size * read_block_number;
				const UINT32 write_virtual_block_number = static_cast<UINT32>(read_virtual_address / destination_block_size);
				const UINT32 write_virtual_block_offset = static_cast<UINT32>(read_virtual_address % destination_block_size);
				dup_extent.SourceFileOffset.QuadPart = *read_physical_address;
				dup_extent.TargetFileOffset.QuadPart = dst_img->AllocateBlockForWrite(write_virtual_block_number) + write_virtual_block_offset;
				dup_extent.ByteCount.QuadPart = source_block_size;
				_ASSERT(dup_extent.SourceFileOffset.QuadPart % ClusterSizeInBytes == 0);
				_ASSERT(dup_extent.TargetFileOffset.QuadPart % ClusterSizeInBytes == 0);
				_ASSERT(dup_extent.ByteCount.QuadPart % ClusterSizeInBytes == 0);
				if (!DeviceIoControl(dst_img->GetFileH(), FSCTL_DUPLICATE_EXTENTS_TO_FILE, &dup_extent, sizeof dup_extent, nullptr, 0, &junk, nullptr))
				{
					die();
				}
			}
		}
	}
	else
	{
		for (UINT32 read_block_number = 0; read_block_number < src_img->GetTableEntriesCount(); read_block_number++)
		{
			if (const std::optional<UINT64> read_physical_address = src_img->ProbeBlock(read_block_number))
			{
				for (UINT32 i = 0; i < source_block_size / destination_block_size; i++)
				{
					const UINT64 read_virtual_address = 1ULL * source_block_size * read_block_number;
					const UINT32 read_block_offset = destination_block_size * i;
					const UINT32 write_virtual_block_number = static_cast<UINT32>((read_virtual_address + read_block_offset) / destination_block_size);
					dup_extent.SourceFileOffset.QuadPart = *read_physical_address + read_block_offset;
					dup_extent.TargetFileOffset.QuadPart = dst_img->AllocateBlockForWrite(write_virtual_block_number);
					dup_extent.ByteCount.QuadPart = destination_block_size;
					_ASSERT(dup_extent.SourceFileOffset.QuadPart % ClusterSizeInBytes == 0);
					_ASSERT(dup_extent.TargetFileOffset.QuadPart % ClusterSizeInBytes == 0);
					_ASSERT(dup_extent.ByteCount.QuadPart % ClusterSizeInBytes == 0);
					if (!DeviceIoControl(dst_img->GetFileH(), FSCTL_DUPLICATE_EXTENTS_TO_FILE, &dup_extent, sizeof dup_extent, nullptr, 0, &junk, nullptr))
					{
						die();
					}
				}
			}
		}
	}

	_ASSERT(dst_img->CheckConvertible(nullptr));
	dst_img->WriteHeader();
	FILE_DISPOSITION_INFO dispos = { FALSE };
	//dispos = { FALSE };
	ATLENSURE(SetFileInformationByHandle(dst_img->GetFileH(), FileDispositionInfo, &dispos, sizeof dispos));
}