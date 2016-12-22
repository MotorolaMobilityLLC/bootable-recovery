/*root_check.h for root_check*/

#ifndef __ROOT_CHECK__
#define __ROOT_CHECK__
#define MTK_ROOT_NORMAL_CHECK	1
#define MTK_ROOT_ADVANCE_CHECK	1
enum
{
    CHECK_PASS,
	CHECK_FAIL,
	CHECK_NO_KEY,
	CHECK_OPEN_FILE_ERR,
	CHECK_MOUNT_ERR,
	CHECK_SYSTEM_FILE_NUM_ERR,
	CHECK_FILE_NOT_MATCH,
	CHECK_LOST_FILE,
	CHECK_ADD_NEW_FILE,
	CHECK_IMAGE_ERR,
};

#define MAX_FILES_IN_SYSTEM 10000
#define INT_BY_BIT 32
#define MASK 0x1f
#define SHIFT 5
#define MAX_ROOT_TO_CHECK 20

#define MD5_LENGTH 16
#define DEVNAME_LENGTH 96
#define PRINTNAME_LENGTH 32

typedef struct{
	char img_devname[DEVNAME_LENGTH];	  /* 96 */
	char img_printname[PRINTNAME_LENGTH];     /* 32 */
	char part_name[PRINTNAME_LENGTH];         /* 32 */
        unsigned int ignore;
	unsigned int duplicate;
	unsigned char computed_md5[MD5_LENGTH];   /* 16 */
	unsigned char expected_md5[MD5_LENGTH];   /* 16 */
	unsigned int image_size;
	unsigned int computed_size;
	unsigned int computed_crc32;
	unsigned int expected_size;
	unsigned int expected_crc32;
}img_info_t;

enum{
#ifdef MTK_ROOT_PRELOADER_CHECK
	PRELOADER,
#endif
	UBOOT,
	UBOOT1,
	BOOTIMG,
	RECOVERYIMG,
	LOGO,
        TEE1,
        TEE2,
	PART_MAX,
};

#define hextoi(c) (((c)-'a'+1)>0?((c)-'a'+10):((c)-'0'))
static void hextoi_md5(unsigned char p_crc[MD5_LENGTH*2]){
	int i = 0;
	for(;i<MD5_LENGTH;i++)
	{
		p_crc[i]=hextoi(p_crc[i*2])*16 + hextoi(p_crc[i*2+1]);
	}
}

int root_check();

#endif

