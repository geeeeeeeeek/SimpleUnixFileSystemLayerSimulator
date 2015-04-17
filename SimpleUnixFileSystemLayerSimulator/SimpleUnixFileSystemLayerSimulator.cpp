
#include "stdafx.h"
#include "PathUtility.h"
#include <iostream>
#include <fstream>
#include <string>

#include <time.h>
#include <stdio.h>
using namespace std;

#define DISKFILENAME TEXT("diskblocks.data")

/////////////////////////
//block信息
////////////////////////
//每个block的大小
#define BLOCK_SIZE 1024
//block的数量
#define BLOCK_NUM 16384

/////////////////////////
//inode信息信息
////////////////////////

//每个inode所能映射的最大BLOCK数量
#define N 14
//inode table中的inode数量
#define INODE_NUM 128

//inode的类型
enum ITYPE{
	FS_UNUSEDINODE =0,	//空inode
	FS_FILE = 1,		//文件inode
	FS_DIRECTORY = 2	//目录inode
};

//inode结构体
typedef struct inode_
{
	int type;				//inode类型,取值为ITYPE中的枚举值
	int size;				//inode所映射的block的总大小,单位是byte
	int block_numbers[N];	//inode对应的block number的列表
} inode_t;

//目录类型的inode对应的block结构
//每个block中有8个entry,每个entry大小为128个byte
//其中每个entry的第0~126个byte为目录/文件名称字符串,第127个byte为该entry对应的inodenumber
#define BLOCK_DIRECTORY_ENTRY_SIZE 128
#define BLOCK_DIRECTORY_ENTRY_NUM 8
#define BLOCK_DIRECTORY_ENTRY_INODENUM_OFFSET 127

/////////////////////////
//	磁盘block分布信息
//  说明:以下所有以OFFSET为后缀的值,均表示对应区域起始block的block_number
//           所有以SIZE为后缀的值,均表示对应区域所占用的block数量
////////////////////////

//boot block的启起始位置
#define BOOT_BLOCK_OFFSET 0
//super block的起始位置
#define SUPER_BLOCK_OFFSET 1

//bit map for free inode区域的起始位置和大小
#define BITMAP_FOR_FREE_BLOCK_OFFSET 2
#define BITMAP_FOR_FREE_BLOCK_SIZE ((BLOCK_NUM/8)/BLOCK_SIZE)

//inode table区域的起始位置和大小
#define INODE_TABLE_OFFSET (BITMAP_FOR_FREE_BLOCK_OFFSET + BITMAP_FOR_FREE_BLOCK_SIZE)
//inodetable实际所需空间byte为单位，
#define INODE_TABLE_SIZE_REAL (INODE_NUM *sizeof(inode_t))
//inodetable所需的block数量
#define INODE_TABLE_SIZE ((INODE_TABLE_SIZE_REAL / BLOCK_SIZE)+1)

//文件block区域的起始位置和大小
#define FILE_BLOCK_OFFSET (INODE_TABLE_OFFSET + INODE_TABLE_SIZE)
#define FILE_BLOCK_SIZE (BLOCK_NUM - FILE_BLOCK_OFFSET)

//文件最大值——无效值
//#define MAX_FILE_SIZE (BLOCK_SIZE * N - 1)
//#define MAX_NAME_LENGT 0

//构建空的diskblocks.data
int createEmptyBlockFile(int blocknum);

//载入inode table
int load_inode_table();
//载入bitmap for free block
int load_bitmap();

//block layer
int BLOCK_NUMBER_TO_BLOCK(int b, byte* block);
int WRITE_BLOCK_BY_BLOCK_NUMBER(int b, byte* block);

//file layer
int INDEX_TO_BLOCK_NUMBER(inode_t i, int index);
int INODE_TO_BLOCK(int offset, inode_t i, byte* block);

//inode number
int INODE_NUMBER_TO_INODE(int inode_number, inode_t* inode);
int INODE_NUMBER_TO_BLOCK(int offset, int inode_number, byte* block);

//file name
int NAME_TO_INODE_NUMBER(char* filename, int dir_inode_num);

//path name layer
int PATH_TO_INODE_NUMBER(char* path, int dir);
//inode table
inode_t inode_table[INODE_NUM];
//bitmap for free block
byte freeblockbitmap[BITMAP_FOR_FREE_BLOCK_SIZE*BLOCK_SIZE];

//打印diskblock结构
int printDiskLayout(){
	printf("block size:%d, block num:%d\n",BLOCK_SIZE, BLOCK_NUM);
	printf("inode num:%d, inode size:%d\n",INODE_NUM , sizeof(inode_t));
	printf("bit map block: %d - %d\tbyte(0x%x - 0x%x)\n", BITMAP_FOR_FREE_BLOCK_OFFSET, BITMAP_FOR_FREE_BLOCK_OFFSET+BITMAP_FOR_FREE_BLOCK_SIZE-1
		, BITMAP_FOR_FREE_BLOCK_OFFSET * BLOCK_SIZE, (BITMAP_FOR_FREE_BLOCK_OFFSET+BITMAP_FOR_FREE_BLOCK_SIZE-1)*BLOCK_SIZE);
	printf("  inode block: %d - %d\tbyte(0x%x - 0x%x)\n", INODE_TABLE_OFFSET, INODE_TABLE_OFFSET+INODE_TABLE_SIZE-1
		, INODE_TABLE_OFFSET * BLOCK_SIZE, (INODE_TABLE_OFFSET+INODE_TABLE_SIZE- 1) * BLOCK_SIZE);
	printf("   file block: %d - %d\tbyte(0x%x - 0x%x)\n\n\n", FILE_BLOCK_OFFSET, FILE_BLOCK_OFFSET+FILE_BLOCK_SIZE-1
		, FILE_BLOCK_OFFSET * BLOCK_SIZE, (FILE_BLOCK_OFFSET+FILE_BLOCK_SIZE - 1) * BLOCK_SIZE);
	return 0;
}

//你可以通过如下两个函数一次性载入和写回inodetable与bitmap

//从diskblocks.data文件中的offset位置开始，读入大小为size的数据，写入dst所指向的内存空间
//调用前请申请好大小为size的空间
int loaddiskcontent(byte* dst, int offset, int size){
	TCHAR *szPath;
	std::ifstream dbfile;

	memset(dst, 0 , size);
	PathUtility::ConstructFullFilePath(&szPath,DISKFILENAME);
	dbfile.open(szPath, ios::in|ios::binary);
	dbfile.seekg(offset,ios::beg);
	dbfile.read((char*)dst,size);
	dbfile.close();

	free(szPath);
	return 0;
}
//从diskblocks.data文件中的offset位置开始，写入大小为size由src所指向的数据
//该操作并不影响文件的其他部分
int savediskcontent(byte* src, int offset, int size){
	TCHAR *szPath;
	std::ofstream dbfile;

	PathUtility::ConstructFullFilePath(&szPath,DISKFILENAME);

	//修改二进制文件的一部分请使用ios::out|ios::binary|ios::in
	dbfile.open(szPath, ios::out|ios::binary|ios::in);
	if(!dbfile.is_open()){
		cout<<"file open failed.\n";
		return -1;
	}
	dbfile.seekp(
		//0,
		offset,
		ios::beg);
	dbfile.write((char*)src,size);
	dbfile.flush();
	dbfile.close();

	free(szPath);
	return 0;
}

/////////////////////////
//	自定义Commands
////////////////////////

int cmd_ls(char* wd);

int cmd_tree(char* abs_path);
int cmd_tree_helper(inode_t inode, int depth);

int cmd_export(char* abs_path);

int cmd_find(char* abs_path, char* key);

int cmd_createFile(char* abs_path, char* filename);
int writeback_helper();

int cmd_insertFile(char* abs_path, char* filepath);

int cmd_createDirectory(char* abs_path, char* dirname);

int cmd_rename(char* abs_path, char* filename, char* newname);

int cmd_syb_link(char* abs_path, char* linked_path);

int cmd_delete(char* abs_path, char* filename);
int cmd_delete_helper(int in_num);

int log_err(int code, char* message);

/////////////////////////
//	主函数
////////////////////////
int load_bitmap(){
	loaddiskcontent(&freeblockbitmap[0u],BITMAP_FOR_FREE_BLOCK_OFFSET*BLOCK_SIZE,BITMAP_FOR_FREE_BLOCK_SIZE*BLOCK_SIZE);
	return 0;
}

int load_inode_table(){
	loaddiskcontent((byte*)&inode_table[0u],INODE_TABLE_OFFSET*BLOCK_SIZE,INODE_TABLE_SIZE_REAL);
	return 0;
}

int _tmain(int argc, char* argv[])
{
	if (argc==1){
		//WTF
		log_err(0,"SimpleUnixFileSystemLayerSimulator <command> <param>...");
		return 0;
	}

	//init
	load_bitmap();
	load_inode_table();


	//command info
	if (strcmp(argv[1],"info") == 0){
		if(argc==2)
			printDiskLayout();
		else
			log_err(0,"info");
	}

	//command ls
	if (strcmp(argv[1],"ls")==0){
		if(argc==3)
			cmd_ls(argv[2]);
		else
			log_err(0,"ls <dir_path>");
	}

	//command tree
	if (strcmp(argv[1],"tree")==0){
		if(argc==3)
			cmd_tree(argv[2]);
		else
			log_err(0,"tree <file_path>");
	}

	//command export
	if (strcmp(argv[1],"export")==0){
		if(argc==3)
			cmd_export(argv[2]);
		else
			log_err(0,"export <file_path>");
	}

	//command find
	if (strcmp(argv[1],"find")==0){
		if(argc==4)
			cmd_find(argv[2],argv[3]);
		else
			log_err(0,"find <dir_path> <key>");
	}

	//command createFile
	if (strcmp(argv[1],"createFile")==0){
		if(argc==4)
			cmd_createFile(argv[2],argv[3]);
		else
			log_err(0,"createFile <dir_path> <file_name>");
	}

	//command insertFile
	if (strcmp(argv[1],"insertFile")==0){
		if(argc==4)
			cmd_insertFile(argv[2],argv[3]);
		else
			log_err(0,"insertFile <dir_path> <file_path>");
	}

	//command createDirectory
	if (strcmp(argv[1],"createDirectory")==0){
		if(argc==4)
			cmd_createDirectory(argv[2],argv[3]);
		else
			log_err(0,"createDirectory <dir_path> <dir_name>");
	}

	//command rename
	if (strcmp(argv[1],"rename")==0){
		if(argc==5)
			cmd_rename(argv[2],argv[3],argv[4]);
		else
			log_err(0,"rename <dir_path> <name> <new_name>");
	}

	//command link
	if (strcmp(argv[1],"link")==0){
		if(argc==4)
			cmd_syb_link(argv[2],argv[3]);
		else
			log_err(0,"link <dir_path> <linked_path>");
	}

	//command delete file
	if (strcmp(argv[1],"delete")==0){
		if(argc==4)
			cmd_delete(argv[2],argv[3]);
		else
			log_err(0,"delete <dir_path> <file_name>");
	}

	
	return 0;
}

int LINK(char* path, int inode_num){
	return 0;
}   

//path name layer
int PATH_TO_INODE_NUMBER(char* path, int dir){
	int ptr;
	string str_path(path);
	if ((ptr = str_path.find("/")) == 0)
		str_path = &path[1];
	
	if ((ptr = str_path.find("/")) == -1)
		//if PLAIN_NAME
		return NAME_TO_INODE_NUMBER(&str_path[0u], dir);
	else{
		string name = str_path.substr(0, ptr);
		str_path.erase(0, ptr + 1);
		return PATH_TO_INODE_NUMBER(&str_path[0u], NAME_TO_INODE_NUMBER(&name[0u], dir));
	}
}

//file name layer
int LOOKUP(char* target_name, int dir_inode_num){
	inode_t inode;
	if (INODE_NUMBER_TO_INODE(dir_inode_num,&inode)==-1)
		return -1;
	if (inode.type!=FS_DIRECTORY)
		return -1;
	for(int offset=0;offset<N*BLOCK_SIZE;offset+=BLOCK_SIZE)
	{
		byte* block = (byte*)malloc(BLOCK_SIZE);
		if (INODE_TO_BLOCK(offset,inode,block)==-1)
			return -1;
		for (int entry=0;entry<BLOCK_DIRECTORY_ENTRY_NUM*BLOCK_DIRECTORY_ENTRY_SIZE;entry+=BLOCK_DIRECTORY_ENTRY_SIZE){
			char* name = (char*)malloc(BLOCK_DIRECTORY_ENTRY_SIZE-1);
			memcpy(name,&block[entry],BLOCK_DIRECTORY_ENTRY_SIZE-1);
			if (strcmp(name,target_name)==0){
				byte* inode_num = (byte*)malloc(1);
				memcpy(inode_num,&block[entry+BLOCK_DIRECTORY_ENTRY_INODENUM_OFFSET],1);
				return *inode_num;
			}
		}
	}
	return -1;
}

int NAME_TO_INODE_NUMBER(char* filename, int dir_inode_num)
{
	return LOOKUP(filename,dir_inode_num);
}

//inode number layer
int INODE_NUMBER_TO_INODE(int inode_number, inode_t* inode){
	if (inode_number<0 || inode_number>INODE_NUM)
		//if inode_number out of the boundary of inode_table
		return -1;
	*inode = inode_table[inode_number];
	return 0;
}

int INODE_NUMBER_TO_BLOCK(int offset, int inode_number, byte* block){
	inode_t inode;
	if(INODE_NUMBER_TO_INODE(inode_number,&inode)==0)
		//if INODE_NUMBER_TO_INODE returns normally
		return INODE_TO_BLOCK(offset,inode,block);
	return -1;
}

//file
int INDEX_TO_BLOCK_NUMBER(inode_t i, int index){
	if (index<0 || index>=N)
		//if index exceeds the limit of blocks
		return	-1;
	return i.block_numbers[index];
}

int INODE_TO_BLOCK(int offset, inode_t i, byte* block){
	int b = INDEX_TO_BLOCK_NUMBER(i,offset/BLOCK_SIZE);
	if (b == -1)
		return -1;
	return BLOCK_NUMBER_TO_BLOCK(b, block);
}

//block
//读取磁盘中block number为b的block中的数据，结果将被写入到将block所指内存空间中，大小为一个block_size
//调用前请使用new或malloc申请足够的空间用于存放结果
int BLOCK_NUMBER_TO_BLOCK(int b, byte* block){
	if(b<0||b>=BLOCK_NUM)
		return -1;

	TCHAR *szPath;
	std::ifstream dbfile;

	memset(block, 0 , BLOCK_SIZE*sizeof(byte));
	PathUtility::ConstructFullFilePath(&szPath,DISKFILENAME);
	dbfile.open(szPath, ios::in|ios::binary);
	dbfile.seekg(b * BLOCK_SIZE,ios::beg);
	dbfile.read((char*)block,BLOCK_SIZE);
	dbfile.close();

	free(szPath);
	return 0;
}
//将block所指的数据写入到磁盘中block number为b的block中，大小为一个block_size
int WRITE_BLOCK_BY_BLOCK_NUMBER(int b, byte* block){
	if(b<0||b>=BLOCK_NUM)
		return -1;

	TCHAR *szPath;
	std::ofstream dbfile;

	PathUtility::ConstructFullFilePath(&szPath,DISKFILENAME);
	dbfile.open(szPath, ios::out|ios::binary|ios::in);
	dbfile.seekp(b * BLOCK_SIZE,ios::beg);
	dbfile.write((char*)block,BLOCK_SIZE);
	dbfile.flush();
	dbfile.close();

	free(szPath);
	return 0;
}

int cmd_ls(char* abs_path){
	int i = PATH_TO_INODE_NUMBER(abs_path,0);
	inode_t inode;
	if(inode.type!=FS_DIRECTORY)
	{
		//if not a dir
		log_err(2,"");
		return -1;
	}
	INODE_NUMBER_TO_INODE(i,&inode);
	for(int offset=0;offset<N*BLOCK_SIZE;offset+=BLOCK_SIZE)
	{
		byte* block = (byte*)malloc(BLOCK_SIZE);
		if (INODE_TO_BLOCK(offset,inode,block)==-1)
		{
			log_err(4,"");
			return -1;
		}
		for (int entry=0;entry<BLOCK_DIRECTORY_ENTRY_NUM*BLOCK_DIRECTORY_ENTRY_SIZE;entry+=BLOCK_DIRECTORY_ENTRY_SIZE){
			char* name = (char*)malloc(BLOCK_DIRECTORY_ENTRY_SIZE-1);
			memcpy(name,&block[entry],BLOCK_DIRECTORY_ENTRY_SIZE-1);
			if (*name)
				cout<<name<<endl;
		}
	}
	return 0;
}

int cmd_export(char* abs_path){
	int i = PATH_TO_INODE_NUMBER(abs_path,0);
	inode_t inode;
	INODE_NUMBER_TO_INODE(i,&inode);
	
	if(inode.type!=FS_FILE)
	{
		//if not a dir
		log_err(3,"");
		return -1;
	}

	//get file name from file path
	string str_path(abs_path);
	int ptr = -1;
	while ((ptr = str_path.find("/"))!=-1){
		str_path.erase(0, ptr + 1);
	}
	
	//prepare output
	ofstream out (str_path);

	int size_remain=inode.size;
	for(int b=0;b<N && size_remain>0;b++){
		byte* block = (byte*)malloc(BLOCK_SIZE);
		int block_number = inode.block_numbers[b];
		if (block_number==0)
			break;
		
		BLOCK_NUMBER_TO_BLOCK(block_number,block);
		out.write((char*)block,min(size_remain,BLOCK_SIZE));
		size_remain-=BLOCK_SIZE;
	}

	//close output
	out.close();
	return 0;
}

int cmd_tree(char* abs_path){
	int i = PATH_TO_INODE_NUMBER(abs_path,0);
	inode_t inode;
	INODE_NUMBER_TO_INODE(i,&inode);
	if(inode.type!=FS_DIRECTORY)
	{
		//if not a dir
		log_err(2,"");
		return -1;
	}
	//get path name
	string str_pathname(abs_path);
	int ptr = -1;
	while ((ptr = str_pathname.find("/"))!=-1)
		str_pathname.erase(0, ptr + 1);
	char* pathname=&str_pathname[0u];


	if (i!=0)
		cout<<"+ "<<pathname<<"("<<i<<")"<<endl;
	else
		cout<<"+ /(0)"<<endl;
	cmd_tree_helper(inode,1);
	return 0;
}

int cmd_tree_helper(inode_t inode, int depth){
	if (inode.type!=FS_DIRECTORY)
		return 1;
	for(int offset=0;offset<N*BLOCK_SIZE;offset+=BLOCK_SIZE)
	{
		byte* block = (byte*)malloc(BLOCK_SIZE);
		if (INODE_TO_BLOCK(offset,inode,block)==-1)
		{
			log_err(4,"");
			return -1;
		}
		for (int entry=0;entry<BLOCK_DIRECTORY_ENTRY_NUM*BLOCK_DIRECTORY_ENTRY_SIZE;entry+=BLOCK_DIRECTORY_ENTRY_SIZE){
			char* name = (char*)malloc(BLOCK_DIRECTORY_ENTRY_SIZE-1);
			memcpy(name,&block[entry],BLOCK_DIRECTORY_ENTRY_SIZE-1);
			if (*name){
				byte* inode_num = (byte*)malloc(1);
				memcpy(inode_num,&block[entry+BLOCK_DIRECTORY_ENTRY_INODENUM_OFFSET],1);

				inode_t in;
				INODE_NUMBER_TO_INODE(int(*inode_num),&in);
				
				//prefix of a line, "+" for dir, "-" for file
				for (int space=0;space<depth*2;space++)
					cout<<" ";
				if((&in)->type==FS_DIRECTORY)
					cout<<"+ "<<name<<"("<<int(*inode_num)<<")"<<endl;
				else
					cout<<"- "<<name<<"("<<int(*inode_num)<<") size: "<<(&in)->size<<endl;

				//recursively search for children of the inode
				if (strcmp(name,".")!=0 && strcmp(name,"..")!=0)
					cmd_tree_helper(in,depth+1);
			}
		}
	}
	return 0;
}

int cmd_find(char* abs_path, char* key){
	int i = PATH_TO_INODE_NUMBER(abs_path,0);
	inode_t inode;

	if(inode.type!=FS_DIRECTORY)
	{
		//if not a dir
		log_err(2,"");
		return -1;
	}

	INODE_NUMBER_TO_INODE(i,&inode);
	for(int offset=0;offset<N*BLOCK_SIZE;offset+=BLOCK_SIZE)
	{
		byte* block = (byte*)malloc(BLOCK_SIZE);
		if (INODE_TO_BLOCK(offset,inode,block)==-1)
		{
			log_err(4,"");
			return -1;
		}
		for (int entry=0;entry<BLOCK_DIRECTORY_ENTRY_NUM*BLOCK_DIRECTORY_ENTRY_SIZE;entry+=BLOCK_DIRECTORY_ENTRY_SIZE){
			char* name = (char*)malloc(BLOCK_DIRECTORY_ENTRY_SIZE-1);
			memcpy(name,&block[entry],BLOCK_DIRECTORY_ENTRY_SIZE-1);
			string str_name(name);
			if (str_name.find(key)!=-1)
				cout<<name<<endl;
		}
	}
	return 0;
}

int cmd_createFile(char* abs_path, char* filename){
	int i = PATH_TO_INODE_NUMBER(abs_path,0);
	inode_t inode;
	INODE_NUMBER_TO_INODE(i,&inode);

	if(inode.type!=FS_DIRECTORY)
	{
		//if not a dir
		log_err(2,"");
		return -1;
	}

	inode_t* in = new inode_t();
	in->type=FS_FILE;
	in->size=0;
	int in_num;
	for (in_num=0;in_num<INODE_NUM;in_num++){
		//if there are free inodes to assign
		if (inode_table[in_num].type==0){
			inode_table[in_num]=*in;
			break;
		}
	}

	if(in_num==INODE_NUM){
		//not enough inodes
		log_err(6,"");
			return -1;}

	for(int offset=0;offset<N*BLOCK_SIZE;offset+=BLOCK_SIZE){
		byte* block = (byte*)malloc(BLOCK_SIZE);
		int b = INDEX_TO_BLOCK_NUMBER(inode,offset/BLOCK_SIZE);

		if (b==0){
			//if a block happens to run out of entries, use a new block
			for(int ii = 0; ii < BITMAP_FOR_FREE_BLOCK_SIZE*BLOCK_SIZE; ii++){
				for(int jj = 0; jj < 8; jj++){
					if(((freeblockbitmap[ii] >> jj) & 1) == 0){
						b = ii * 8 + jj;
						//update bitmap
						freeblockbitmap[ii] |= 1 << jj;
						
						//update inode table
						inode.size+=BLOCK_SIZE;
						inode.block_numbers[inode.size/BLOCK_SIZE-1]=b;
						inode_table[i]=inode;

						goto BLOCK_ASSIGNED;
					}
				}
			}
		}

		BLOCK_ASSIGNED:

		if (INODE_TO_BLOCK(offset,inode,block)==-1)
		{
			log_err(4,"");
			return -1;
		}

		for (int entry=0;entry<BLOCK_DIRECTORY_ENTRY_NUM*BLOCK_DIRECTORY_ENTRY_SIZE;entry+=BLOCK_DIRECTORY_ENTRY_SIZE){
			char* name = (char*)malloc(BLOCK_DIRECTORY_ENTRY_SIZE-1);
			memcpy(name,&block[entry],BLOCK_DIRECTORY_ENTRY_SIZE-1);
			if (!*name){

				if(strcmp(name, filename) == 0)
					//duplicated filename
					return -1;

				memset(&block[entry],0,BLOCK_DIRECTORY_ENTRY_SIZE);
				int filename_length = string(filename).size();
				memcpy(&block[entry],filename,string(filename).size());
				memcpy(&block[entry+BLOCK_DIRECTORY_ENTRY_INODENUM_OFFSET],&in_num,1);
				
				//write back block
				WRITE_BLOCK_BY_BLOCK_NUMBER(b,block);
				writeback_helper();
				return 0; 
			}
		}
	}
	
	
	return -1;
}
int writeback_helper(){
	//write back bitmap
	savediskcontent((byte*)&freeblockbitmap[0], BITMAP_FOR_FREE_BLOCK_OFFSET * BLOCK_SIZE, BITMAP_FOR_FREE_BLOCK_SIZE*BLOCK_SIZE);
	//write back inode table
	savediskcontent((byte*)&inode_table[0], INODE_TABLE_OFFSET * BLOCK_SIZE, INODE_TABLE_SIZE_REAL);
	return 0;
}

int cmd_insertFile(char* abs_path, char* filepath){
	int i = PATH_TO_INODE_NUMBER(abs_path,0);
	inode_t inode;
	INODE_NUMBER_TO_INODE(i,&inode);

	if(inode.type!=FS_DIRECTORY)
	{
		//if not a dir
		log_err(2,"");
		return -1;
	}


	//get file name from file path
	string str_filename(filepath);
	int ptr = -1;
	while ((ptr = str_filename.find("/"))!=-1)
		str_filename.erase(0, ptr + 1);
	char* filename=&str_filename[0u];

	//file size
	ifstream file(filepath);
	int begin = file.tellg();
	file.seekg (0, file.end);
	int end = file.tellg();
	
	
	int file_size=end-begin;
	char* file_text = (char*)malloc((file_size/BLOCK_SIZE+1)*BLOCK_SIZE);
	file.seekg (0, ios::beg);
	file.read (file_text, file_size);
	file.close();
	//free block size
	int free_blocks_num=0;
	for(int ii = 0; ii < BITMAP_FOR_FREE_BLOCK_SIZE*BLOCK_SIZE; ii++)
		for(int jj = 0; jj < 8; jj++){
			if(((freeblockbitmap[ii] >> jj) & 1) == 0)
				free_blocks_num++;
			if(free_blocks_num==N)
				goto ENOUGH_FREE_BLOCKS;
		}
	//not enough free blocks
		log_err(6,"");
	return -1;

ENOUGH_FREE_BLOCKS:

	inode_t* in = new inode_t();
	in->type=FS_FILE;
	in->size=0;
	int in_num;
	for (in_num=0;in_num<INODE_NUM;in_num++){
		//if there are free inodes to assign
		if (inode_table[in_num].type==0){
			inode_table[in_num]=*in;
			break;
		}
	}

	if(in_num==INODE_NUM){
		//not enough inodes
		log_err(6,"");
		return -1;}

	//copy the file
	int index=0;
	for(int ii = 0; ii < BITMAP_FOR_FREE_BLOCK_SIZE*BLOCK_SIZE; ii++){
		for(int jj = 0; jj < 8; jj++)
		{
			if(((freeblockbitmap[ii] >> jj) & 1) == 0)
			{
				int b = ii * 8 + jj;
				//update bitmap
				freeblockbitmap[ii] |= 1 << jj;

				//update inode table
				in->size+=BLOCK_SIZE;
				in->block_numbers[in->size/BLOCK_SIZE-1]=b;

				WRITE_BLOCK_BY_BLOCK_NUMBER(b,(byte*)&file_text[index*BLOCK_SIZE]);
				index++;
				if (index*BLOCK_SIZE>=file_size){
					in->size = file_size;
					goto FILE_COPIED;
				}
			}
			
		}
	}

FILE_COPIED:

	inode_table[in_num]=*in;
//	writeback_helper();


	//insert the file to dir
	for(int offset=0;offset<N*BLOCK_SIZE;offset+=BLOCK_SIZE){
		byte* block = (byte*)malloc(BLOCK_SIZE);
		int b = INDEX_TO_BLOCK_NUMBER(inode,offset/BLOCK_SIZE);

		if (b==0){
			//if a block happens to run out of entries, use a new block
			for(int ii = 0; ii < BITMAP_FOR_FREE_BLOCK_SIZE*BLOCK_SIZE; ii++){
				for(int jj = 0; jj < 8; jj++){
					if(((freeblockbitmap[ii] >> jj) & 1) == 0){
						b = ii * 8 + jj;
						//update bitmap
						freeblockbitmap[ii] |= 1 << jj;
						
						//update inode table
						inode.size+=BLOCK_SIZE;
						inode.block_numbers[inode.size/BLOCK_SIZE-1]=b;
						inode_table[i]=inode;

						goto BLOCK_ASSIGNED;
					}
				}
			}
		}

		BLOCK_ASSIGNED:

		if (INODE_TO_BLOCK(offset,inode,block)==-1)
		{
			log_err(4,"");
			return -1;
		}

		for (int entry=0;entry<BLOCK_DIRECTORY_ENTRY_NUM*BLOCK_DIRECTORY_ENTRY_SIZE;entry+=BLOCK_DIRECTORY_ENTRY_SIZE){
			char* name = (char*)malloc(BLOCK_DIRECTORY_ENTRY_SIZE-1);
			memcpy(name,&block[entry],BLOCK_DIRECTORY_ENTRY_SIZE-1);
			if (!*name){

				if(strcmp(name, filename) == 0)
					//duplicated filename
					return -1;

				memset(&block[entry],0,BLOCK_DIRECTORY_ENTRY_SIZE);
				int filename_length = string(filename).size();
				memcpy(&block[entry],filename,string(filename).size());
				memcpy(&block[entry+BLOCK_DIRECTORY_ENTRY_INODENUM_OFFSET],&in_num,1);
				
				//write back block
				WRITE_BLOCK_BY_BLOCK_NUMBER(b,block);
				writeback_helper();
				return 0; 
			}
		}
	}
	
	
	return -1;



	
}

int cmd_createDirectory(char* abs_path, char* dirname){
	int i = PATH_TO_INODE_NUMBER(abs_path,0);
	inode_t inode;
	INODE_NUMBER_TO_INODE(i,&inode);

	if(inode.type!=FS_DIRECTORY)
	{
		//if not a dir
		log_err(2,"");
		return -1;
	}

	inode_t* in = new inode_t();
	in->type=FS_DIRECTORY;
	in->size=0;
	int in_num;
	for (in_num=0;in_num<INODE_NUM;in_num++){
		//if there are free inodes to assign
		if (inode_table[in_num].type==0){
			inode_table[in_num]=*in;
			break;
		}
	}

	if(in_num==INODE_NUM){
		//not enough inodes
		log_err(6,"");
			return -1;
	}
	//in, find blocks for "." and ".."
	int sel;
	for(int ii = 0; ii < BITMAP_FOR_FREE_BLOCK_SIZE*BLOCK_SIZE; ii++){
		for(int jj = 0; jj < 8; jj++){
			if(((freeblockbitmap[ii] >> jj) & 1) == 0){
				sel = ii * 8 + jj;
				//update bitmap
				freeblockbitmap[ii] |= 1 << jj;

				//update inode table
				in->size=BLOCK_SIZE;
				in->block_numbers[0]=sel;
				inode_table[in_num]=*in;
				goto SEL_ASSIGNED;
			}
		}
	}
	
	// not enough blocks
	log_err(6,"");
	return -1;
SEL_ASSIGNED:

	//in, insert "." and ".."
	byte* bl = (byte*)malloc(BLOCK_SIZE);
	if (BLOCK_NUMBER_TO_BLOCK(sel,bl)==-1)
	{
		log_err(4,"");
		return -1;
	}
			

	memset(&bl[0],0,BLOCK_DIRECTORY_ENTRY_SIZE);
	memcpy(&bl[0],".",1);
	memcpy(&bl[0+BLOCK_DIRECTORY_ENTRY_INODENUM_OFFSET],&in_num,1);

	memset(&bl[BLOCK_DIRECTORY_ENTRY_SIZE],0,BLOCK_DIRECTORY_ENTRY_SIZE);
	memcpy(&bl[BLOCK_DIRECTORY_ENTRY_SIZE],"..",2);
	memcpy(&bl[BLOCK_DIRECTORY_ENTRY_SIZE+BLOCK_DIRECTORY_ENTRY_INODENUM_OFFSET],&i,1);

	//write back block
	WRITE_BLOCK_BY_BLOCK_NUMBER(sel,bl);
	writeback_helper();


	//inode
	for(int offset=0;offset<N*BLOCK_SIZE;offset+=BLOCK_SIZE){
		byte* block = (byte*)malloc(BLOCK_SIZE);
		int b = INDEX_TO_BLOCK_NUMBER(inode,offset/BLOCK_SIZE);

		if (b==0){
			//if a block happens to run out of entries, use a new block
			for(int ii = 0; ii < BITMAP_FOR_FREE_BLOCK_SIZE*BLOCK_SIZE; ii++){
				for(int jj = 0; jj < 8; jj++){
					if(((freeblockbitmap[ii] >> jj) & 1) == 0){
						b = ii * 8 + jj;
						//update bitmap
						freeblockbitmap[ii] |= 1 << jj;
						
						//update inode table
						inode.size+=BLOCK_SIZE;
						inode.block_numbers[inode.size/BLOCK_SIZE-1]=b;
						inode_table[i]=inode;

						goto BLOCK_ASSIGNED;
					}
				}
			}
		}

		BLOCK_ASSIGNED:

		if (INODE_TO_BLOCK(offset,inode,block)==-1)
		{
			log_err(4,"");
			return -1;
		}
			

		for (int entry=0;entry<BLOCK_DIRECTORY_ENTRY_NUM*BLOCK_DIRECTORY_ENTRY_SIZE;entry+=BLOCK_DIRECTORY_ENTRY_SIZE){
			char* name = (char*)malloc(BLOCK_DIRECTORY_ENTRY_SIZE-1);
			memcpy(name,&block[entry],BLOCK_DIRECTORY_ENTRY_SIZE-1);
			if (!*name){

				if(strcmp(name, dirname) == 0)
					//duplicated filename
					return -1;

				memset(&block[entry],0,BLOCK_DIRECTORY_ENTRY_SIZE);
				int filename_length = string(dirname).size();
				memcpy(&block[entry],dirname,string(dirname).size());
				memcpy(&block[entry+BLOCK_DIRECTORY_ENTRY_INODENUM_OFFSET],&in_num,1);
				
				//write back block
				WRITE_BLOCK_BY_BLOCK_NUMBER(b,block);
				writeback_helper();

				return 0; 
			}
		}
	}
	return -1;
}

int cmd_rename(char* abs_path, char* filename, char* newname)
{
	int i = PATH_TO_INODE_NUMBER(abs_path,0);
	inode_t inode;
	INODE_NUMBER_TO_INODE(i,&inode);

	if(inode.type!=FS_DIRECTORY)
	{
		//if not a dir
		log_err(2,"");
		return -1;
	}
	
	for(int offset=0;offset<N*BLOCK_SIZE;offset+=BLOCK_SIZE){
		byte* block = (byte*)malloc(BLOCK_SIZE);
		int b = INDEX_TO_BLOCK_NUMBER(inode,offset/BLOCK_SIZE);

		if (INODE_TO_BLOCK(offset,inode,block)==-1)
		{
			log_err(4,"");
			return -1;
		}

		for (int entry=0;entry<BLOCK_DIRECTORY_ENTRY_NUM*BLOCK_DIRECTORY_ENTRY_SIZE;entry+=BLOCK_DIRECTORY_ENTRY_SIZE){
			char* name = (char*)malloc(BLOCK_DIRECTORY_ENTRY_SIZE-1);
			memcpy(name,&block[entry],BLOCK_DIRECTORY_ENTRY_SIZE-1);
			if (*name){

				if(strcmp(name, filename) == 0)
				{
					memset(&block[entry],0,BLOCK_DIRECTORY_ENTRY_SIZE-1);
					int newname_length = string(newname).size();
					memcpy(&block[entry],newname,string(newname).size());

					//write back block
					WRITE_BLOCK_BY_BLOCK_NUMBER(b,block);
					writeback_helper();
					return 0; 
				}
			}
		}
	}
	
	log_err(233,"");
	return -1;
}

int cmd_syb_link(char* abs_path, char* linked_path)
{
	int i = PATH_TO_INODE_NUMBER(abs_path,0);
	inode_t inode;
	INODE_NUMBER_TO_INODE(i,&inode);

	if(inode.type!=FS_DIRECTORY)
	{
		//if not a dir
		log_err(2,"");
		return -1;
	}

	int in_num = PATH_TO_INODE_NUMBER(linked_path,0);

	//get file name from file path
	string str_linkedname(linked_path);
	int ptr = -1;
	while ((ptr = str_linkedname.find("/"))!=-1)
		str_linkedname.erase(0, ptr + 1);
	char* linkedname=&str_linkedname[0u];

	for(int offset=0;offset<N*BLOCK_SIZE;offset+=BLOCK_SIZE){
		byte* block = (byte*)malloc(BLOCK_SIZE);
		int b = INDEX_TO_BLOCK_NUMBER(inode,offset/BLOCK_SIZE);

		if (b==0){
			//if a block happens to run out of entries, use a new block
			for(int ii = 0; ii < BITMAP_FOR_FREE_BLOCK_SIZE*BLOCK_SIZE; ii++){
				for(int jj = 0; jj < 8; jj++){
					if(((freeblockbitmap[ii] >> jj) & 1) == 0){
						b = ii * 8 + jj;
						//update bitmap
						freeblockbitmap[ii] |= 1 << jj;
						
						//update inode table
						inode.size+=BLOCK_SIZE;
						inode.block_numbers[inode.size/BLOCK_SIZE-1]=b;
						inode_table[i]=inode;

						goto BLOCK_ASSIGNED;
					}
				}
			}
		}

		BLOCK_ASSIGNED:

		if (INODE_TO_BLOCK(offset,inode,block)==-1)
		{
			log_err(4,"");
			return -1;
		}

		for (int entry=0;entry<BLOCK_DIRECTORY_ENTRY_NUM*BLOCK_DIRECTORY_ENTRY_SIZE;entry+=BLOCK_DIRECTORY_ENTRY_SIZE){
			char* name = (char*)malloc(BLOCK_DIRECTORY_ENTRY_SIZE-1);
			memcpy(name,&block[entry],BLOCK_DIRECTORY_ENTRY_SIZE-1);
			if (!*name){

				if(strcmp(name, linkedname) == 0)
				{
					//duplicated filename
					log_err(5,"");
					return -1;
				}
					

				memset(&block[entry],0,BLOCK_DIRECTORY_ENTRY_SIZE);
				int filename_length = string(linkedname).size();
				memcpy(&block[entry],linkedname,string(linkedname).size());
				memcpy(&block[entry+BLOCK_DIRECTORY_ENTRY_INODENUM_OFFSET],&in_num,1);
				
				//write back block
				WRITE_BLOCK_BY_BLOCK_NUMBER(b,block);
				writeback_helper();
				return 0; 
			}
		}
	}
	
	log_err(233,"");
	return -1;
} 

int cmd_delete(char* abs_path, char* filename)
{
	int i = PATH_TO_INODE_NUMBER(abs_path,0);
	inode_t inode;
	INODE_NUMBER_TO_INODE(i,&inode);

	if(inode.type!=FS_DIRECTORY)
	{
		//if not a dir
		log_err(2,"");
		return -1;
	}
		

	for(int offset=0;offset<N*BLOCK_SIZE;offset+=BLOCK_SIZE){
		byte* block = (byte*)malloc(BLOCK_SIZE);
		int b = INDEX_TO_BLOCK_NUMBER(inode,offset/BLOCK_SIZE);

		if (INODE_TO_BLOCK(offset,inode,block)==-1)
		{
			log_err(4,"");
			return -1;
		}
			

		for (int entry=0;entry<BLOCK_DIRECTORY_ENTRY_NUM*BLOCK_DIRECTORY_ENTRY_SIZE;entry+=BLOCK_DIRECTORY_ENTRY_SIZE){
			char* name = (char*)malloc(BLOCK_DIRECTORY_ENTRY_SIZE-1);
			memcpy(name,&block[entry],BLOCK_DIRECTORY_ENTRY_SIZE-1);
			if (*name){

				if(strcmp(name, filename) == 0)
				{
					byte* inode_num = (byte*)malloc(1);
					memcpy(inode_num,&block[entry+BLOCK_DIRECTORY_ENTRY_INODENUM_OFFSET],1);

					//recycle free blocks
					cmd_delete_helper((int)*inode_num);
					memset(&block[entry],0,BLOCK_DIRECTORY_ENTRY_SIZE);

					//write back block
					WRITE_BLOCK_BY_BLOCK_NUMBER(b,block);
					writeback_helper();
					return 0; 
				}
				
			}
		}
	}
	
	log_err(233,"");
	return -1;
}

int cmd_delete_helper(int in_num)
{
	inode_t in;
	INODE_NUMBER_TO_INODE(in_num,&in);

	if(in.type!=FS_FILE)
	{
		//if not a dir
		log_err(2333,"Only file deletion supported.");
		return -1;
	}
		

	for(int i =0;in.block_numbers[i]!=0;i++)
	{
		int ii=in.block_numbers[i];
		freeblockbitmap[ii/8]&=~(1<<(8-ii%8));
	}

	inode_table[in_num]=inode_t();
	return 0;
}

int log_err(int code, char* message){
	switch(code){
		case -1:cout<<"ERR: "<<message<<" is not recognized as an internal command."<<endl;break;
		case 0:cout<<"ERR: bad parameters. \nUsage: "<<message<<endl;break;
		case 1:cout<<"ERR: reduntant arguments found. \nUsage: "<<message<<endl;break;
		case 2:cout<<"ERR: path is not a directory."<<message<<endl;break;
		case 3:cout<<"ERR: path is not a file."<<message<<endl;break;
		case 4:cout<<"ERR: FS internal error."<<message<<endl;break;
		case 5:cout<<"ERR: duplicated names."<<message<<endl;break;
		case 233:cout<<"ERR: unknown."<<message<<endl;break;
		default:cout<<"ERR: "<<message<<endl;
	}
	return 0;
}
