#include "Cache.hpp"

#include <string.h>
#include <unistd.h>

#include "Md5.hpp"

std::string PATH = "/home/lanstat/projects/confiared/my-cdn/cache/";

char *Cache::GetUID(char *url) {
    std::string aux(url);
    std::cout<< "LAN_[" << __FILE__ << ":" << __LINE__ << "] "<< strlen(url) << std::endl;
    auto uid = md5(aux);
    std::string uri;
    uri.append(PATH);
    uri.append(uid);
    return strdup(uri.c_str());
}

void *Cache::ZhMalloc(size_t size) {
    void *buf = malloc(size);
    if (!buf) {
        fprintf(stderr, "Fatal error: unable to allocate memory.\n");
        exit(1);
    }
    return buf;
}

bool Cache::IsValid(char *uri, struct stat *path_stat) {
    return (stat(uri, path_stat) == 0);
}

void Cache::Read(char *uri, off_t file_size, struct iovec *iov) {
    int fd;

    char *buf = (char *)ZhMalloc(file_size);
    fd = open(uri, O_RDONLY);
    if (fd < 0) {
        std::cout << "LAN_[" << __FILE__ << ":" << __LINE__ << "] "
                  << "open" << std::endl;
        exit(1);
    }

    int ret = read(fd, buf, file_size);
    if (ret < file_size) {
        fprintf(stderr, "Encountered a short read.\n");
    }
    close(fd);

    iov->iov_base = buf;
    iov->iov_len = file_size;
}

void Cache::Write(const std::string &uid, char *raw) {
    std::string uri;
    uri.append(PATH);
    uri.append(uid);

    /*
    std::ofstream stream(uri, std::ios::out | std::ios::binary);
    std::copy(raw.begin(), raw.end(), std::ostreambuf_iterator<char>(stream));

    stream.close();
    */
}

std::vector<unsigned char> Cache::ReadPrev(const std::string &uid) {
    std::vector<unsigned char> buffer;
    std::string uri;
    uri.append(PATH);
    uri.append(uid);

    size_t bytes_avail = 128;
    size_t to_read = 128;

    std::ifstream stream(uri.c_str());
    if (stream) {
        buffer.resize(bytes_avail);

        stream.read((char *)(&buffer[0]), to_read);
        size_t counted = stream.gcount();
        buffer.resize(counted);
    }
    stream.close();

    return buffer;
}

void Cache::WritePrev(const std::string &uri, std::vector<unsigned char> raw) {
    std::ofstream stream(uri, std::ios::out | std::ios::binary);
    std::copy(raw.begin(), raw.end(), std::ostreambuf_iterator<char>(stream));

    stream.close();
}
