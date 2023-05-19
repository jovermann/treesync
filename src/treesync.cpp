// treesync - diff and synchronize directory trees
//
// Copyright (c) 2022-2023 Johannes Overmann
//
// Distributed under the Boost Software License, Version 1.0.
// (See accompanying file LICENSE_1_0.txt or copy at https://www.boost.org/LICENSE_1_0.txt)

#include <regex>
#include <iostream>
#include <filesystem>
#include <utility>
#include <functional>
#include "CommandLineParser.hpp"
#include "MiscUtils.hpp"
#include "UnitTest.hpp"

/// Output colors.
class TerminalColors
{
public:
    TerminalColors(bool noColor)
    {
        if (noColor)
        {
            ins.clear();
            del.clear();
            nor.clear();
        }
    }

    // Foreground:
    // 30 - black
    // 31 - red
    // 32 - green
    // 33 - yellow
    // 34 - blue
    // 35 - magenta
    // 36 - cyan
    // 37 - white
    //
    // Background:
    // 4x
    //
    // Style:
    // 00 - normal
    // 01 - bright foreground
    // 02 - dark foreground
    // 03 - italics
    // 04 - ?
    // 05 - blink
    // 06 - ?
    // 07 - inverse
    std::string ins = "\33[32m";
    std::string del = "\33[31m";
    std::string nor = "\33[00m";
};

class TreeDiff
{
public:
    class Params
    {
    public:
        std::string srcdir;
        std::string dstdir;
        bool ignoreDirs{};
        bool ignoreSpecial{};
        bool ignoreForks{};
        bool followSymlinks{};
        bool ignoreContent{};
        bool normalizeFilenames{};

        /// Called for items which are in src only.
        std::function<void(const std::filesystem::directory_entry &, const std::filesystem::path &, Params&)> srcOnly;

        /// Called for items which are in dst only.
        std::function<void(const std::filesystem::path &, const std::filesystem::directory_entry &, Params&)> dstOnly;

        /// Called for regular files with the same content, symlink with the same link target, char and block device with the same major/minor and fifos and sockets.
        std::function<void(const std::filesystem::directory_entry &, const std::filesystem::directory_entry &, Params&)> match;

        /// Called for regular files with different content, symlinks with different link targets, char and block devices with different major/minor.
        std::function<void(const std::filesystem::directory_entry &, const std::filesystem::directory_entry &, Params&)> mismatch;

        /// Called when src and dst are of different type.
        std::function<void(const std::filesystem::directory_entry &, const std::filesystem::directory_entry &, Params&)> typeMismatch;

        /// Called before src and dst are scanned.
        std::function<void(const std::filesystem::directory_entry &, const std::filesystem::directory_entry &, Params&)> progressDirs;

        /// Called before src and dst (same name and same type) are compared.
        std::function<void(const std::filesystem::directory_entry &, const std::filesystem::directory_entry &, Params&)> progressFiles;

        /// Called for ignored dir (if ignoreDirs).
        std::function<void(const std::filesystem::directory_entry &, Params&)> ignoredDir;

        /// Called for ignored special files (if ignoreSpecial).
        std::function<void(const std::filesystem::directory_entry &, Params&)> ignoredFile;
    };

    TreeDiff(const Params &params_) : params(params_)
    {
    }

    void process()
    {
        processDir(std::filesystem::directory_entry(params.srcdir), std::filesystem::directory_entry(params.dstdir));
    }

private:
    // Returns true if no difference is found.
    bool processDir(const std::filesystem::directory_entry &src, const std::filesystem::directory_entry &dst)
    {
        // Report progress.
        progressDirs(src, dst);

        // Read src dir.
        std::map<std::string, std::filesystem::directory_entry> srcmap;
        for (const std::filesystem::directory_entry &entry: std::filesystem::directory_iterator(src))
        {
            if (params.ignoreForks && ut1::hasPrefix(entry.path().filename(), "._"))
            {
                continue;
            }
            std::string fname = entry.path().filename();
            if (params.normalizeFilenames)
            {
                fname = ut1::toNfd(fname);
            }
            srcmap[fname] = entry;
        }

        // Read dst dir.
        std::map<std::string, std::filesystem::directory_entry> dstmap;
        for (const std::filesystem::directory_entry &entry: std::filesystem::directory_iterator(dst))
        {
            if (params.ignoreForks && ut1::hasPrefix(entry.path().filename(), "._"))
            {
                continue;
            }
            std::string fname = entry.path().filename();
            if (params.normalizeFilenames)
            {
                fname = ut1::toNfd(fname);
            }
            dstmap[fname] = entry;
        }

        // Compare dirs by iterating over both lists simultaneously.
        auto itsrc = srcmap.begin();
        auto itdst = dstmap.begin();
        bool noDifferenceFound = true;
        for (;;)
        {
            if ((itsrc == srcmap.end()) && (itdst == dstmap.end()))
            {
                break;
            }

            // Check for deletion.
            if ((itsrc != srcmap.end()) && ((itdst == dstmap.end()) || (itsrc->first < itdst->first)))
            {
                // Src only.
                srcOnly(itsrc->second, dst.path());
                noDifferenceFound = false;
                itsrc++;
            }
            else if ((itdst != dstmap.end()) && ((itsrc == srcmap.end()) || (itsrc->first > itdst->first)))
            {
                // Dst only.
                dstOnly(src.path(), itdst->second);
                noDifferenceFound = false;
                itdst++;
            }
            else
            {
                // Names are matching. Compare type.
                assert(itsrc != srcmap.end());
                assert(itdst != dstmap.end());
                assert(itsrc->first == itdst->first);

                ut1::FileType srctype = ut1::getFileType(itsrc->second, params.followSymlinks);
                ut1::FileType dsttype = ut1::getFileType(itdst->second, params.followSymlinks);
                if (srctype != dsttype)
                {
                    // File type does not match. Generate a type mismatch.
                    typeMismatch(itsrc->second, itdst->second);
                    noDifferenceFound = false;
                }
                else
                {
                    // Names and file types match. Compare content.
                    ut1::FileType type = ut1::getFileType(itsrc->second, params.followSymlinks);
                    if (type != ut1::FT_DIR)
                    {
                        progressFiles(itsrc->second, itdst->second);
                    }
                    switch (type)
                    {
                    case ut1::FT_REGULAR:
                        if ((itsrc->second.file_size() == itdst->second.file_size()) &&
                            (params.ignoreContent || (ut1::readFile(itsrc->second.path()) == ut1::readFile(itdst->second.path()))))
                        {
                            match(itsrc->second, itdst->second);
                        }
                        else
                        {
                            mismatch(itsrc->second, itdst->second);
                            noDifferenceFound = false;
                        }
                        break;

                    case ut1::FT_DIR:
                        if (!params.ignoreDirs)
                        {
                            if (!processDir(itsrc->second, itdst->second))
                            {
                                noDifferenceFound = false;
                            }
                        }
                        else
                        {
                            ignoredDir(itsrc->second);
                            ignoredDir(itdst->second);
                        }
                        break;

                    case ut1::FT_SYMLINK:
                        if (std::filesystem::read_symlink(itsrc->second) == std::filesystem::read_symlink(itdst->second))
                        {
                            match(itsrc->second, itdst->second);
                        }
                        else
                        {
                            mismatch(itsrc->second, itdst->second);
                            noDifferenceFound = false;
                        }
                        break;

                    case ut1::FT_FIFO:
                    case ut1::FT_SOCKET:
                        if (!params.ignoreSpecial)
                        {
                            // Fifos and sockets have no content and always match.
                            match(itsrc->second, itdst->second);
                        }
                        else
                        {
                            ignoredFile(itsrc->second);
                            ignoredFile(itdst->second);
                        }
                        break;

                    case ut1::FT_BLOCK:
                    case ut1::FT_CHAR:
                        if (!params.ignoreSpecial)
                        {
                            if (ut1::getStat(itsrc->second).getRDev() == ut1::getStat(itdst->second).getRDev())
                            {
                                match(itsrc->second, itdst->second);
                            }
                            else
                            {
                                mismatch(itsrc->second, itdst->second);
                                noDifferenceFound = false;
                            }
                        }
                        else
                        {
                            ignoredFile(itsrc->second);
                            ignoredFile(itdst->second);
                        }
                        break;

                    case ut1::FT_NON_EXISTING:
                        // Will never occur unless files vanish after directory scanning.
                        // Broken symbolic links are reported through FT_BROKEN_SYMLINK.
                        ignoredFile(itsrc->second);
                        ignoredFile(itdst->second);
                        break;
                    }
                }

                itsrc++;
                itdst++;
            }
        }

        return noDifferenceFound;
    }

    void srcOnly(const std::filesystem::directory_entry &src, const std::filesystem::path &dstdir)
    {
        if (params.srcOnly)
        {
            params.srcOnly(src, dstdir, params);
        }
    }

    void dstOnly(const std::filesystem::path &srcdir, const std::filesystem::directory_entry &dst)
    {
        if (params.dstOnly)
        {
            params.dstOnly(srcdir, dst, params);
        }
    }

    void match(const std::filesystem::directory_entry &src, const std::filesystem::directory_entry &dst)
    {
        if (params.match)
        {
            params.match(src, dst, params);
        }
    }

    void mismatch(const std::filesystem::directory_entry &src, const std::filesystem::directory_entry &dst)
    {
        if (params.mismatch)
        {
            params.mismatch(src, dst, params);
        }
    }

    void typeMismatch(const std::filesystem::directory_entry &src, const std::filesystem::directory_entry &dst)
    {
        if (params.typeMismatch)
        {
            params.typeMismatch(src, dst, params);
        }
    }

    void progressDirs(const std::filesystem::directory_entry &src, const std::filesystem::directory_entry &dst)
    {
        if (params.progressDirs)
        {
            params.progressDirs(src, dst, params);
        }
    }

    void progressFiles(const std::filesystem::directory_entry &src, const std::filesystem::directory_entry &dst)
    {
        if (params.progressFiles)
        {
            params.progressFiles(src, dst, params);
        }
    }

    void ignoredDir(const std::filesystem::directory_entry &entry)
    {
        if (params.ignoredDir)
        {
            params.ignoredDir(entry, params);
        }
    }

    void ignoredFile(const std::filesystem::directory_entry &entry)
    {
        if (params.ignoredFile)
        {
            params.ignoredFile(entry, params);
        }
    }

    Params params;
};


/// Print directory entry.
void printDirectoryEntry(const std::filesystem::directory_entry &entry, const std::string &prefix, const std::string &suffix, bool followSymlinks, bool recursive)
{
    std::cout << prefix << ut1::getFileTypeStr(entry, followSymlinks) << " " << entry.path() << suffix << "\n";
    if (!recursive || !entry.is_directory())
    {
        return;
    }
    for (const std::filesystem::directory_entry &entry_: std::filesystem::directory_iterator(entry))
    {
        printDirectoryEntry(entry_, prefix, suffix, followSymlinks, recursive);
    }
}


/// Main.
int main(int argc, char* argv[])
{
    // Run unit tests and exit if enabled at compile time.
    UNIT_TEST_RUN();

    try
    {
        // Command line options.
        ut1::CommandLineParser cl("treesync", "Sync or diff two directory trees, recursively.\n"
                                  "\n"
                                  "Usage: $programName [OPTIONS] SRCDIR DSTDIR\n"
                                  "\n"
                                  "Compare SRCDIR with DSTDIR and print differences (--diff or no option) or update DSTDIR in certain ways (--new, --delete or --update). SRCDIR is never modified.\n",
                                  "\n"
                                  "$programName version $version *** Copyright (c) 2022-2023 Johannes Overmann *** https://github.com/jovermann/treesync",
                                  "0.1.3");

        cl.addHeader("\nFile/dir processing options:\n");
        cl.addOption('N', "new", "Copy files/dirs which only appear in SRCDIR into DSTDIR.");
        cl.addOption('D', "delete", "Delete files/dirs in DSTDIR which do not appear in SRCDIR.");
        cl.addOption('U', "update", "Copy files/dirs which either only appear in SRCDIR or which are newer (mtime) than the corresponding file in DSTDIR or which differ in type into DSTDIR. Implies --new.");
        cl.addOption(' ', "diff", "Print differences and do not change anything. This is also the default if none of --new/--delete or --update are specified. Note: Differences are printed in the view of going from DSTDIR to SRCDIR, so usually treediff NEW OLD (unlike diff OLD NEW).");
        cl.addOption(' ', "ignore-dirs", "Just process the two specified directories. Ignore subdirectories.");
        cl.addOption(' ', "ignore-special", "Just process regular files, dirs and symbolic links. Ignore block/char devices, pipes and sockets.");
        cl.addOption('F', "ignore-forks", "Ignore all files and dirs starting with '._' (Apple resource forks).");
        cl.addOption(' ', "follow-symlinks", "Follow symlinks. Without this (default) symlinks are compared as distinct filesystem objects.");
        cl.addOption('c', "create-missing-dst", "Create DSTDIR if it does not exist for --new/--update.");
        cl.addOption(' ', "copy-ins", "Copy insertions to DIR during --diff. DSTDIR is not modified.", "DIR");
        cl.addOption(' ', "copy-del", "Copy deletions to DIR during --diff. DSTDIR is not modified.", "DIR");
//        cl.addOption('p', "preserve", "Copy mtime for --new and --update."); // todo

        cl.addHeader("\nMatching options:\n");
        cl.addOption('C', "ignore-content", "Ignore file content when comparing files. Just compare their size and assume files with the same size are identical.");
        cl.addOption('T', "ignore-mtime", "Ignore mtime for --update and always assume the SRC to be newer than DST if they are different, e.g. always overwrite DST with SRC if SRC and DST are different.");
        cl.addOption('Z', "normalize-filenames", "Apply unicode canonical nornmalization (NFD) before comparing filenames. Specify this if you want different filenames which only differ in the NFC/NFD encoding to compare as equal.");

        cl.addHeader("\nVerbose / common options:\n");
        cl.addOption(' ', "show-matches", "Show matching files for --diff instead of only showing differences (default).");
        cl.addOption(' ', "show-subtree", "For new/deleted dirs show all files/dirs in these trees (default is to just show the new/deleted dir itself).");
        cl.addOption('v', "verbose", "Increase verbosity. Specify multiple times to be more verbose.");
        cl.addOption('n', "no-color", "Do not color output.");
        cl.addOption('d', "dummy-mode", "Do not write/change/delete anything.");

        // Parse command line options.
        cl.parse(argc, argv);

        if (cl.getArgs().size() != 2)
        {
            cl.error("Please specify SRCDIR and DSTDIR.\n");
        }

        // Get options.
        unsigned verbose = unsigned(cl.getUInt("verbose"));
        bool showMatches = cl("show-matches");
        bool showSubtree = cl("show-subtree");
        bool dummyMode = cl("dummy-mode");
        bool ignoreMtime = cl("ignore-mtime");
        bool noColor = cl("no-color");
        bool createMissingDst = cl("create-missing-dst");
        bool preserve = false; // cl("preserve"); // todo
        std::string copyIns = cl.getStr("copy-ins");
        std::string copyDel = cl.getStr("copy-del");

        // Determine mode.
        bool new_ = cl("new");
        bool delete_ = cl("delete");
        bool update = cl("update");
        bool diff = cl("diff");
        if (new_ || delete_ || update)
        {
            if (diff)
            {
                cl.error("--diff may not be specified with --new/--delete/--update\n");
            }
        }
        else
        {
            // No mode specified. Assume --diff.
            diff = true;
        }
        if (update)
        {
            // --update implies --new.
            new_ = true;
        }

        TerminalColors col(noColor);

        TreeDiff::Params params;
        params.srcdir = cl.getArgs()[0];
        params.dstdir = cl.getArgs()[1];
        params.ignoreDirs = cl("ignore-dirs");
        params.ignoreSpecial = cl("ignore-special");
        params.ignoreForks = cl("ignore-forks");
        params.followSymlinks = cl("follow-symlinks");
        params.ignoreContent = cl("ignore-content");
        params.normalizeFilenames = cl("normalize-filenames");
        std::filesystem::copy_options copy_options_base = params.followSymlinks ? std::filesystem::copy_options::none : std::filesystem::copy_options::copy_symlinks;

        params.srcOnly = ([&](const std::filesystem::directory_entry &src, const std::filesystem::path &dstdir, TreeDiff::Params &params_)
        {
            if (diff)
            {
                printDirectoryEntry(src, col.ins + "+ ", col.nor, params_.followSymlinks, showSubtree);
                if (!copyIns.empty())
                {
                    if (!std::filesystem::exists(copyIns))
                    {
                        if (verbose)
                        {
                            std::cout << "Creating --copy-ins destination dir \"" << copyIns << "\"\n";
                        }
                        if (!dummyMode)
                        {
                            std::filesystem::create_directories(copyIns);
                        }
                    }
                    if (verbose)
                    {
                        std::cout << "Copying (--copy-ins) " << ut1::getFileTypeStr(src, params_.followSymlinks) << " " << src.path() << " -> " << copyIns << "\n";
                    }
                    if (!dummyMode)
                    {
                        std::filesystem::copy(src.path(), copyIns, copy_options_base);
                    }
                }
            }
            if (new_)
            {
                std::filesystem::path dst = dstdir / src.path().filename();
                if (verbose)
                {
                    std::cout << "Copying (new) " << ut1::getFileTypeStr(src, params_.followSymlinks) << " " << src.path() << " -> " << dst << "\n";
                }
                if (!dummyMode)
                {
                    std::filesystem::copy(src.path(), dst, std::filesystem::copy_options::recursive | copy_options_base);
                }
            }
        });

        params.dstOnly = ([&](const std::filesystem::path &srcdir, const std::filesystem::directory_entry &dst, TreeDiff::Params &params_)
        {
            (void)srcdir;
            if (diff)
            {
                printDirectoryEntry(dst, col.del + "- ", col.nor, params_.followSymlinks, showSubtree);
                if (!copyDel.empty())
                {
                    if (!std::filesystem::exists(copyDel))
                    {
                        if (verbose)
                        {
                            std::cout << "Creating --copy-ins destination dir \"" << copyDel << "\"\n";
                        }
                        if (!dummyMode)
                        {
                            std::filesystem::create_directories(copyDel);
                        }
                    }
                    if (verbose)
                    {
                        std::cout << "Copying (--copy-del) " << ut1::getFileTypeStr(dst, params_.followSymlinks) << " " << dst.path() << " -> " << copyDel << "\n";
                    }
                    if (!dummyMode)
                    {
                        std::filesystem::copy(dst.path(), copyDel, copy_options_base);
                    }
                }
            }
            if (delete_)
            {
                if (verbose)
                {
                    std::cout << "Deleting " << ut1::getFileTypeStr(dst, params_.followSymlinks) << " " << dst.path() << "\n";
                }
                if (!dummyMode)
                {
                    std::filesystem::remove_all(dst.path());
                }
            }
        });

        params.match = ([&](const std::filesystem::directory_entry &src, const std::filesystem::directory_entry &dst, TreeDiff::Params &params_)
        {
            if (diff && showMatches)
            {
                std::cout << "= " << ut1::getFileTypeStr(src, params_.followSymlinks) << " " << src.path() << " and " << ut1::getFileTypeStr(dst, params_.followSymlinks) << " " << dst.path() << "\n";
            }
            if (update)
            {
                if ((!ignoreMtime) && (ut1::getLastWriteTime(src, params_.followSymlinks) > ut1::getLastWriteTime(dst, params_.followSymlinks)))
                {
                    if (verbose)
                    {
                        std::cout << "Updating mtime " << ut1::getFileTypeStr(src, params_.followSymlinks) << " " << src.path() << " -> " << dst.path() << "\n";
                    }
                    if (!dummyMode)
                    {
                        if (preserve)
                        {
                            ut1::setLastWriteTime(dst, ut1::getLastWriteTime(src, params_.followSymlinks), params_.followSymlinks);
                        }
                    }
                }
            }
        });

        params.mismatch = ([&](const std::filesystem::directory_entry &src, const std::filesystem::directory_entry &dst, TreeDiff::Params &params_)
        {
            if (diff)
            {
                std::string srcInfo;
                std::string dstInfo;
                if (ut1::getFileType(src, params_.followSymlinks) == ut1::FT_SYMLINK)
                {
                    srcInfo = " -> \"" + std::filesystem::read_symlink(src).string() + "\"";
                    dstInfo = " -> \"" + std::filesystem::read_symlink(dst).string() + "\"";
                }
                else
                {
                    if (src.file_size() != dst.file_size())
                    {
                        dstInfo = " (size " + std::to_string(src.file_size()) + " != " + std::to_string(dst.file_size()) + ")";
                    }
                    else
                    {
                        dstInfo = " (same size, different content)";
                    }

                }
                std::cout << "Diff: " << ut1::getFileTypeStr(src, params_.followSymlinks) << " " << src.path() << srcInfo << " and " << ut1::getFileTypeStr(dst, params_.followSymlinks) << " " << dst.path() << dstInfo << "\n";
            }
            if (update)
            {
                if (ignoreMtime || (ut1::getLastWriteTime(src, params_.followSymlinks) > ut1::getLastWriteTime(dst, params_.followSymlinks)))
                {
                    if (verbose)
                    {
                        std::cout << "Copying (update) " << ut1::getFileTypeStr(src, params_.followSymlinks) << " " << src.path() << " -> " << dst.path() << "\n";
                    }
                    if (!dummyMode)
                    {
                        if (dst.is_symlink())
                        {
                            // Neither overwrite_existing nor copy_symlinks replace symlinks, so delete the dst symlink first.
                            std::filesystem::remove(dst.path());
                        }
                        std::filesystem::copy(src.path(), dst.path(), std::filesystem::copy_options::overwrite_existing | copy_options_base);
                    }
                }
            }
        });

        params.typeMismatch = ([&](const std::filesystem::directory_entry &src, const std::filesystem::directory_entry &dst, TreeDiff::Params &params_)
        {
            if (diff)
            {
                std::cout << "Type mismatch: " << ut1::getFileTypeStr(src, params_.followSymlinks) << " " << src.path() << " and " << ut1::getFileTypeStr(dst, params_.followSymlinks) << " " << dst.path() << "\n";
            }
            if (update)
            {
                if (verbose)
                {
                    std::cout << "Copying (type mismatch) " << ut1::getFileTypeStr(src, params_.followSymlinks) << " " << src.path() << " -> " << dst.path() << "\n";
                }
                if (!dummyMode)
                {
                    std::filesystem::remove_all(dst.path());
                    std::filesystem::copy(src.path(), dst.path(), std::filesystem::copy_options::recursive | copy_options_base);
                }
            }
        });

        params.progressDirs = ([&](const std::filesystem::directory_entry &src, const std::filesystem::directory_entry &dst, TreeDiff::Params &params_)
        {
            (void)params_;
            if (verbose >= 2)
            {
                std::cout << "Processing dirs " << src.path() << " and " << dst.path() << "\n";
            }
        });

        params.progressFiles = ([&](const std::filesystem::directory_entry &src, const std::filesystem::directory_entry &dst, TreeDiff::Params &params_)
        {
            (void)params_;
            if (verbose >= 3)
            {
                std::cout << "Processing " << ut1::getFileTypeStr(src, params_.followSymlinks) << " " << src.path() << " and " << ut1::getFileTypeStr(dst, params_.followSymlinks) << " " << dst.path() << "\n";
            }
        });

        params.ignoredDir = ([&](const std::filesystem::directory_entry &entry, TreeDiff::Params &params_)
        {
            (void)params_;
            if (diff || verbose)
            {
                std::cout << "Ignoring dir " << entry.path() << "\n";
            }
        });

        params.ignoredFile = ([&](const std::filesystem::directory_entry &entry, TreeDiff::Params &params_)
        {
            if (diff || verbose)
            {
                std::cout << "Ignoring " << ut1::getFileTypeStr(entry, params_.followSymlinks) << " " << entry.path() << "\n";
            }
        });

        if (new_ && (!std::filesystem::exists(params.dstdir)) && createMissingDst)
        {
            if (verbose)
            {
                std::cout << "Creating destination dir \"" << params.dstdir << "\"\n";
            }
            std::filesystem::create_directories(params.dstdir);
        }

        // Check for src/dst directory existence.
        if (!std::filesystem::exists(params.srcdir))
        {
            cl.reportErrorAndExit("SRCDIR \"" + params.srcdir + "\" does not exist!\n");
        }
        if (!std::filesystem::is_directory(params.srcdir))
        {
            cl.reportErrorAndExit("SRCDIR \"" + params.srcdir + "\" is not a directory!\n");
        }
        if (!std::filesystem::exists(params.dstdir))
        {
            cl.reportErrorAndExit("DSTDIR \"" + params.dstdir + "\" does not exist!\n");
        }
        if (!std::filesystem::is_directory(params.dstdir))
        {
            cl.reportErrorAndExit("DSTDIR \"" + params.dstdir + "\" is not a directory!\n");
        }

        TreeDiff treediff(params);
        treediff.process();
    }
    catch (const std::exception &e)
    {
        ut1::CommandLineParser::reportErrorAndExit(e.what());
    }

    return 0;
}
