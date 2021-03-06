#include "Logger.h"
#include "CDirectory.h"

CDirectory::CDirectory(INODEPTR node, SimpleFilesystem &_fs) : dirnode(node), fs(_fs) 
{
    blocksize = fs.bio->blocksize;
    std::lock_guard<std::mutex> lock(dirnode->GetMutex());
    if (node->type != INODETYPE::dir) throw ENOTDIR;
}

void CDirectory::ForEachEntry(std::function<FOREACHENTRYRET(DIRENTRY &de)> f)
{
    std::lock_guard<std::mutex> lock(dirnode->GetMutex());
    ForEachEntryNonBlocking(f);
}

void CDirectory::ForEachEntryNonBlocking(std::function<FOREACHENTRYRET(DIRENTRY &de)> f)
{
    int8_t buf[blocksize];
    uint64_t ofs = 0;
    int64_t size = 0;
    do
    {
        size = dirnode->ReadInternal(buf, ofs, blocksize);
        assert((size==blocksize) || (size == 0));
        int ndirperblock = size/sizeof(DIRENTRY);
        auto *de = (DIRENTRY*)buf;
        for(int j=0; j<ndirperblock; j++)
        {
            FOREACHENTRYRET ret = f(*de);
            if (ret == FOREACHENTRYRET::QUIT) return;
            if (ret == FOREACHENTRYRET::WRITEANDQUIT)
            {
                dirnode->WriteInternal(buf, ofs, blocksize);
                return;
            }
            de++;
        }
        ofs += size;
    } while(size == blocksize);
}


void CDirectory::Create()
{
    int ndirperblock = blocksize/sizeof(DIRENTRY);
    DIRENTRY buf[ndirperblock];
    dirnode->Write((int8_t*)buf, 0, blocksize);
}

int CDirectory::MakeDirectory(const std::string& name)
{
    DIRENTRY e("");
    Find(name, e);
    if (e.id != CFragmentDesc::INVALIDID) throw EEXIST;
    return fs.MakeDirectory(*this, name);
}

int CDirectory::MakeFile(const std::string& name)
{
    DIRENTRY e("");
    Find(name, e);
    if (e.id != CFragmentDesc::INVALIDID) throw EEXIST;
    return fs.MakeFile(*this, name);
}

void CDirectory::AddEntry(const DIRENTRY &denew)
{
    LOG(LogLevel::DEEP) << "AddDirEntry '" << denew.name << "' id=" << denew.id;
    bool written = false;
    std::lock_guard<std::mutex> lock(dirnode->GetMutex());
    ForEachEntryNonBlocking([&](DIRENTRY &de)
    {
        if (de.id == CFragmentDesc::INVALIDID)
        {
            memcpy(&de, &denew, sizeof(DIRENTRY));
            written = true;
            //printf("Add entry %s\n", de.name);
            return FOREACHENTRYRET::WRITEANDQUIT;
        }
        return FOREACHENTRYRET::OK;
    });
    if (written) return;
    int8_t buf[blocksize];
    memset(buf, 0xFF, blocksize);
    auto *de = (DIRENTRY*)buf;
    memcpy(de, &denew, sizeof(DIRENTRY));
    dirnode->WriteInternal(buf, dirnode->size, blocksize);
}

void CDirectory::RemoveEntry(const std::string &name, DIRENTRY &e)
{
    e.id = CFragmentDesc::INVALIDID;
    LOG(LogLevel::DEEP) << "RemoveDirEntry '" << name << "' in dir '" << dirnode->name << "'";
    ForEachEntry([&](DIRENTRY &de)
    {
        if (de.id == CFragmentDesc::INVALIDID) return FOREACHENTRYRET::OK;
        if (strncmp(de.name, name.c_str(), 64+32) == 0)
        {
            memcpy(&e, &de, sizeof(DIRENTRY));
            de.id = CFragmentDesc::INVALIDID;
            return FOREACHENTRYRET::WRITEANDQUIT;
        }
        return FOREACHENTRYRET::OK;
    });
}

void CDirectory::Find(const std::string &s, DIRENTRY &e)
{
    e.id = CFragmentDesc::INVALIDID;
    ForEachEntry([&](DIRENTRY &de)
    {
        if (de.id == CFragmentDesc::INVALIDID) return FOREACHENTRYRET::OK;
        if (strncmp(de.name, s.c_str(), 64+32) == 0)
        {
            memcpy(&e, &de, sizeof(DIRENTRY));
            return FOREACHENTRYRET::QUIT;
        }
        return FOREACHENTRYRET::OK;
    });
}

bool CDirectory::IsEmpty()
{
    bool empty = true;
    ForEachEntry([&](DIRENTRY &de)
    {
        if (de.id == CFragmentDesc::INVALIDID) return FOREACHENTRYRET::OK;
        empty = false;
        return FOREACHENTRYRET::QUIT;
    });
    return empty;
}

void CDirectory::List()
{
    printf("  Listing of id=%i, name='%s', with size=%lli\n", dirnode->id, dirnode->name.c_str(), (long long int)dirnode->size);
    int n = -1;
    //const char *typestr[] = {"UNK", "DIR", "FILE"};
    ForEachEntry([&](DIRENTRY &de)
    {
        n++;
        if (de.id == CFragmentDesc::INVALIDID) return FOREACHENTRYRET::OK;
        printf("  %3i: %7i '%s'\n", n, de.id, de.name);
        return FOREACHENTRYRET::OK;
    });
}
