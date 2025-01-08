#include "dentry.h"
#include "clib/string.h"

Dentry::Dentry(Inode* inode, Dentry* parent, const char* name) : inode(inode), parent(parent),
                                                                 name(new char[strlen(name) + 1])
{
	strcpy((char*) this->name, name);
	if (parent)
		parent->rc++;
	inode->rc++;
}

Dentry::~Dentry()
{
	parent->rc--;
	inode->rc--;
	delete[] name;
}
