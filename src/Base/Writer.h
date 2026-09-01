/***************************************************************************
 *   Copyright (c) 2011 Jürgen Riegel <juergen.riegel@web.de>              *
 *                                                                         *
 *   This file is part of the FreeCAD CAx development system.              *
 *                                                                         *
 *   This library is free software; you can redistribute it and/or         *
 *   modify it under the terms of the GNU Library General Public           *
 *   License as published by the Free Software Foundation; either          *
 *   version 2 of the License, or (at your option) any later version.      *
 *                                                                         *
 *   This library  is distributed in the hope that it will be useful,      *
 *   but WITHOUT ANY WARRANTY; without even the implied warranty of        *
 *   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the         *
 *   GNU Library General Public License for more details.                  *
 *                                                                         *
 *   You should have received a copy of the GNU Library General Public     *
 *   License along with this library; see the file COPYING.LIB. If not,    *
 *   write to the Free Software Foundation, Inc., 59 Temple Place,         *
 *   Suite 330, Boston, MA  02111-1307, USA                                *
 *                                                                         *
 ***************************************************************************/

#ifndef BASE_WRITER_H
#define BASE_WRITER_H


#include <set>
#include <unordered_set>
#include <string>
#include <sstream>
#include <vector>
#include <cassert>
#include <memory>

#ifdef _MSC_VER
#include <zipios++/zipios-config.h>
#endif
#include <zipios++/zipfile.h>
#include <zipios++/zipinputstream.h>
#include <zipios++/zipoutputstream.h>
#include <zipios++/meta-iostreams.h>

#include "FileInfo.h"
#include "Type.h"


namespace Base
{

class Persistence;
class SequencerLauncher;


/** The Writer class
 * This is an important helper class for the store and retrieval system
 * of persistent objects in FreeCAD.
 * \see Base::Persistence
 * \author Juergen Riegel
 */
class BaseExport Writer
{

public:
    explicit Writer(short indent_size=2);
    virtual ~Writer();

    /// switch the writer in XML only mode (no files allowed)
    void setForceXML(int on);
    /// check on state
    int isForceXML() const;

    /// split xml among each object
    void setSplitXML(bool on);
    /// check whether to split xml among each object
    bool isSplitXML() const;

    /// set preference of binary output
    void setPreferBinary(bool on);
    bool isPreferBinary() const;

    void setFileVersion(int);
    int getFileVersion() const;

    /** Document schema version being written, 0 when unknown.
     *
     * Only the document save path sets this. Writers that produce a
     * self-contained stream of their own -- object export, for one -- leave it
     * at 0 and get the format that does not depend on a document-wide table.
     */
    void setSchemaVersion(int);
    int getSchemaVersion() const;

    /** The name to write for a type, which the schema decides
     *
     * A type name goes into the file as the type= attribute, so renaming a
     * registered class changes the wire format. Schema 4 is upstream's
     * format and predates every rename this fork has made, so it gets the
     * name the type used to have (Type::getLegacyName); schema 5 and later
     * get the current one. A type that was never renamed answers the same
     * either way.
     *
     * Reading needs none of this -- Type::addLegacyName makes the old
     * spelling resolve at any schema -- so this exists only so a document
     * deliberately saved back to schema 4 stays readable by what reads
     * schema 4.
     */
    const char* typeName(const Base::Type& type) const;

    /// put the next entry with a give name
    virtual void putNextEntry(const char *filename, const char *objName=nullptr);

    /// insert a file as CDATA section in the XML file
    void insertAsciiFile(const char* FileName);
    /// insert a binary file BASE64 coded as CDATA section in the XML file
    void insertBinFile(const char* FileName, unsigned base64_line_size=80);
    /// insert text string as CDATA
    void insertText(const std::string& str);

    /** @name additional file writing */
    //@{
    /// add a write request of a persistent object
    const std::string &addFile(const char* Name, const Base::Persistence *Object);
    /// add a write request of a persistent object
    const std::string &addFile(const std::string &Name, const Base::Persistence *Object) {
        return addFile(Name.c_str(),Object);
    }
    /// process the requested file storing
    virtual void writeFiles() = 0;

    /** Report the file loop's progress through a launcher the caller owns.
     *
     * Writing the requested files is where a save of a large document spends
     * its time -- one entry per parked shape, and the BRep behind it -- so it
     * is the loop a progress indicator has to see. The launcher is not made
     * here because only the TOP launcher reports at all: one started under a
     * live blocking one is a silent no-op, so the save owns a single launcher
     * spanning its phases and lends it to the writer for this one.
     *
     * Pass nullptr, the default, and the loop runs unreported as before.
     */
    void setProgress(SequencerLauncher* seq)
    {
        progressSeq = seq;
    }

    /** Where the launcher stands, as the base of a phase about to start.
     *
     * A save is a sequence of phases -- the objects, the blob entries, the
     * requested files -- and none of them knows its own size until the one
     * before it has run. Each takes the base once and restates the total from
     * it, so the bar never goes backwards and each phase owns its own stretch.
     * Zero when nobody is watching.
     */
    std::size_t progressBase() const;

    /** Tick one item of a phase of `count` items that began at `base`.
     *
     * The total is restated on every step rather than fixed up front: a file
     * list grows while it is walked, an entry being written may add another.
     */
    void stepProgress(std::size_t base, std::size_t count);

    /** Whether this writer can carry a document-wide store.
     *
     * A store is one entry that many properties point into
     * (docs/SharedShapeStorage.md), which pays off for a writer producing one
     * self-contained archive and costs for one that does not. The recovery
     * writer keeps a file per property and rewrites only what changed; give it
     * a store and every autosave rewrites the document's whole geometry.
     */
    virtual bool supportsSharedStore() const { return false; }
    /// get all registered file names
    const std::vector<std::string>& getFilenames() const;
    /// Set mode
    void setMode(const std::string& mode);
    /// Set modes
    void setModes(const std::set<std::string>& modes);
    /// Get mode
    bool getMode(const std::string& mode) const;
    /// Get modes
    std::set<std::string> getModes() const;
    /// Clear mode
    void clearMode(const std::string& mode);
    /// Clear modes
    void clearModes();
    //@}

    /// Obtain the current file name
    const char *getCurrentFileName() const {
        return ObjectName.c_str();
    }

    /** @name Error handling */
    //@{
    void addError(const std::string&);
    bool hasErrors() const;
    void clearErrors();
    std::vector<std::string> getErrors() const;
    //@}

    /** @name pretty formatting for XML */
    //@{
    /// get the current indentation
    const char* ind() const
    {
        return indBuf;
    }
    /// increase indentation by one tab
    void incInd();
    /// decrease indentation by one tab
    void decInd();
    //@}

    virtual std::ostream& Stream() = 0;

    /** Create an output stream for storing character content
     * The input is assumed to be valid character with
     * the current XML encoding, and will be enclosed inside
     * CDATA section.  The stream will scan the input and
     * properly escape any CDATA ending inside.
     *
     * @param format: If Base64Encoded, the input will be base64 encoded before storing.
     *                If Raw, the input is assumed to be valid character with
     *                the current XML encoding, and will be enclosed inside
     *                CDATA section.  The stream will scan the input and
     *                properly escape any CDATA ending inside.
     *
     * @param line_size: specifies the line size of the base64 output
     *
     * @return Returns an output stream.
     *
     * You must call endCharStream() to end the current character stream.
     */
    std::ostream& beginCharStream(CharStreamFormat format = CharStreamFormat::Raw, unsigned line_size = 80);

    /** Create an output stream for storing base 64 encoded binary data
     *
     * @param line_size: specifies the line size of the base64 output
     * @return Returns an output stream.
     */
    std::ostream& beginBase64Stream(unsigned line_size = 80)
    {
        return beginCharStream(CharStreamFormat::Base64Encoded, line_size);
    }

    /** End the current character output stream
     * @return Returns the normal writer stream for convenience
     */
    std::ostream& endCharStream();
    /// Return the current character output stream
    std::ostream& charStream();

    // NOLINTBEGIN

protected:
    std::string getUniqueFileName(const char* Name);
    struct FileEntry
    {
        std::string FileName;
        const Base::Persistence* Object;
    };
    std::vector<FileEntry> FileList;
    std::vector<std::string> FileNames;
    std::unordered_set<std::string> FileNameSet;
    std::vector<std::string> Errors;
    std::set<std::string> Modes;

    int indent {0};
    short indent_size {2};
    char indBuf[65] {};

    int schemaVersion {0};
    int forceXML {0};
    bool splitXML {false};
    bool preferBinary {true};

    int fileVersion {1};
    // NOLINTEND

    /// Borrowed from the save that owns it; null when nobody is watching.
    SequencerLauncher* progressSeq {nullptr};

public:
    Writer(const Writer&) = delete;
    Writer(Writer&&) = delete;
    Writer& operator=(const Writer&) = delete;
    Writer& operator=(Writer&&) = delete;

private:
    /// name for underlying file saves
    std::string ObjectName;
    std::unique_ptr<std::ostream> CharStream;
    CharStreamFormat charStreamFormat {CharStreamFormat::Raw};
};


/** The ZipWriter class
 * This is an important helper class implementation for the store and retrieval system
 * of persistent objects in FreeCAD.
 * \see Base::Persistence
 * \author Juergen Riegel
 */
class BaseExport ZipWriter: public Writer
{
public:
    explicit ZipWriter(const char* FileName);
    explicit ZipWriter(std::ostream&);
    ~ZipWriter() override;

    void writeFiles() override;

    /// One archive, written once: what a shared store is for.
    bool supportsSharedStore() const override { return true; }

    std::ostream& Stream() override
    {
        return ZipStream;
    }

    void setComment(const char* str)
    {
        ZipStream.setComment(str);
    }
    void setLevel(int level)
    {
        ZipStream.setLevel(level);
    }
    void putNextEntry(const char *filename, const char *objName=nullptr) override;

    ZipWriter(const ZipWriter&) = delete;
    ZipWriter(ZipWriter&&) = delete;
    ZipWriter& operator=(const ZipWriter&) = delete;
    ZipWriter& operator=(ZipWriter&&) = delete;

private:
    zipios::ZipOutputStream ZipStream;
};

/** The StringWriter class
 * This is an important helper class implementation for the store and retrieval system
 * of objects in FreeCAD.
 * \see Base::Persistence
 * \author Juergen Riegel
 */
class BaseExport StringWriter: public Writer
{

public:
    StringWriter();

    void clear()
    {
        StrStream.str("");
    }
    std::ostream& Stream() override
    {
        return StrStream;
    }
    std::string getString() const
    {
        return StrStream.str();
    }
    void writeFiles() override;

private:
    std::ostringstream StrStream;
};

/*! The FileWriter class
  This class writes out the data into files into a given directory name.
  \see Base::Persistence
  \author Werner Mayer
 */
class BaseExport FileWriter: public Writer
{
public:
    explicit FileWriter(const char* DirName);
    ~FileWriter() override;

    void putNextEntry(const char *filename, const char *objName=nullptr) override;
    void writeFiles() override;

    std::ostream& Stream() override
    {
        return FileStream;
    }
    void close()
    {
        FileStream.close();
    }
    /*!
     This method can be re-implemented in sub-classes to avoid
     to write out certain objects. The default implementation
     always returns true.
     */
    virtual bool shouldWrite(const std::string& name, const Base::Persistence* Object) const;

    /// Directory the entries are written into.
    const std::string& getDirName() const
    {
        return DirName;
    }

    FileWriter(const FileWriter&) = delete;
    FileWriter(FileWriter&&) = delete;
    FileWriter& operator=(const FileWriter&) = delete;
    FileWriter& operator=(FileWriter&&) = delete;

protected:
    // NOLINTBEGIN
    std::string DirName;
    std::ofstream FileStream;
    // NOLINTEND
};


}  // namespace Base


#endif  // BASE_WRITER_H
