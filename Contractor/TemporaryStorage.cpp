/*
 open source routing machine
 Copyright (C) Dennis Luxen, others 2010

 This program is free software; you can redistribute it and/or modify
 it under the terms of the GNU AFFERO General Public License as published by
 the Free Software Foundation; either version 3 of the License, or
 any later version.

 This program is distributed in the hope that it will be useful,
 but WITHOUT ANY WARRANTY; without even the implied warranty of
 MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 GNU General Public License for more details.

 You should have received a copy of the GNU Affero General Public License
 along with this program; if not, write to the Free Software
 Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
 or see http://www.gnu.org/licenses/agpl.txt.
 */

#include "TemporaryStorage.h"

TemporaryStorage::TemporaryStorage() {
    tempDirectory = boost::filesystem::temp_directory_path();
}

TemporaryStorage & TemporaryStorage::GetInstance(){
    static TemporaryStorage runningInstance;
    return runningInstance;
}

TemporaryStorage::~TemporaryStorage() {
    removeAll();
}

void TemporaryStorage::removeAll() {
    boost::mutex::scoped_lock lock(mutex);
    for(unsigned slot_id = 0; slot_id < vectorOfStreamDatas.size(); ++slot_id) {
        deallocateSlot(slot_id);
    }
    vectorOfStreamDatas.clear();
}

int TemporaryStorage::allocateSlot() {
    boost::mutex::scoped_lock lock(mutex);
    try {
        vectorOfStreamDatas.push_back(StreamData());
        //SimpleLogger().Write() << "created new temporary file: " << vectorOfStreamDatas.back().pathToTemporaryFile;
    } catch(boost::filesystem::filesystem_error & e) {
        abort(e);
    }
    return vectorOfStreamDatas.size() - 1;
}

void TemporaryStorage::deallocateSlot(int slotID) {
    try {
        StreamData & data = vectorOfStreamDatas[slotID];
        boost::mutex::scoped_lock lock(*data.readWriteMutex);
        if(!boost::filesystem::exists(data.pathToTemporaryFile)) {
            return;
        }
        if(data.streamToTemporaryFile->is_open()) {
            data.streamToTemporaryFile->close();
        }

        boost::filesystem::remove(data.pathToTemporaryFile);
    } catch(boost::filesystem::filesystem_error & e) {
        abort(e);
    }
}

void TemporaryStorage::writeToSlot(int slotID, char * pointer, std::streamsize size) {
    try {
        StreamData & data = vectorOfStreamDatas[slotID];
        boost::mutex::scoped_lock lock(*data.readWriteMutex);
        BOOST_ASSERT_MSG(
            data.writeMode,
            "Writing after first read is not allowed"
        );
        data.streamToTemporaryFile->write(pointer, size);
    } catch(boost::filesystem::filesystem_error & e) {
        abort(e);
    }
}
void TemporaryStorage::readFromSlot(int slotID, char * pointer, std::streamsize size) {
    try {
        StreamData & data = vectorOfStreamDatas[slotID];
        boost::mutex::scoped_lock lock(*data.readWriteMutex);
        if(data.writeMode) {
            data.writeMode = false;
            data.streamToTemporaryFile->seekg(0, data.streamToTemporaryFile->beg);
        }
        data.streamToTemporaryFile->read(pointer, size);
    } catch(boost::filesystem::filesystem_error & e) {
        abort(e);
    }
}

unsigned TemporaryStorage::getFreeBytesOnTemporaryDevice() {
    boost::filesystem::space_info tempSpaceInfo;
    try {
        tempSpaceInfo = boost::filesystem::space(tempDirectory);
    } catch(boost::filesystem::filesystem_error & e) {
        abort(e);
    }
    return tempSpaceInfo.available;
}

boost::filesystem::fstream::pos_type TemporaryStorage::tell(int slotID) {
    boost::filesystem::fstream::pos_type position;
    try {
        StreamData & data = vectorOfStreamDatas[slotID];
        boost::mutex::scoped_lock lock(*data.readWriteMutex);
        position = data.streamToTemporaryFile->tellp();
    } catch(boost::filesystem::filesystem_error & e) {
        abort(e);
   }
    return position;
}

void TemporaryStorage::abort(boost::filesystem::filesystem_error& ) {
    removeAll();
}

void TemporaryStorage::seek(int slotID, boost::filesystem::fstream::pos_type position) {
    try {
        StreamData & data = vectorOfStreamDatas[slotID];
        boost::mutex::scoped_lock lock(*data.readWriteMutex);
        data.streamToTemporaryFile->seekg(position);
    } catch(boost::filesystem::filesystem_error & e) {
        abort(e);
    }
}
