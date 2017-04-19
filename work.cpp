#include <fcntl.h>
#include <iostream>
#include <string.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <unistd.h>
//------------------------------------------------------------------------------
ssize_t sock_fd_write(int aSock, void *aBuf, ssize_t aSize, int aFd) {
  iovec iov;
  iov.iov_base = aBuf;
  iov.iov_len = aSize;

  msghdr msg;
  msg.msg_name = 0;
  msg.msg_namelen = 0;
  msg.msg_iov = &iov;
  msg.msg_iovlen = 1;

  if (aFd != -1) {
    union {
      cmsghdr hdr;
      char control[CMSG_SPACE( sizeof(int) )];
    } cmsgu;

    msg.msg_control = cmsgu.control;
    msg.msg_controllen = sizeof(cmsgu.control);

    auto cmsg = CMSG_FIRSTHDR(&msg);
    cmsg->cmsg_len = CMSG_LEN( sizeof(int) );
    cmsg->cmsg_level = SOL_SOCKET;
    cmsg->cmsg_type = SCM_RIGHTS;

    std::cout << "passing fd " << aFd << std::endl;
    *reinterpret_cast< int * >( CMSG_DATA(cmsg) ) = aFd;
  } else {
      msg.msg_control = NULL;
      msg.msg_controllen = 0;

      std::cout << "not passing fd" << std::endl;
  }

  auto const ret = sendmsg(aSock, &msg, 0);

  if (ret < 0) {
    std::cerr << "Fault to sendmsg : " << strerror(errno) << std::endl;
  }

  return ret;
}
//------------------------------------------------------------------------------
ssize_t sock_fd_read(int aSock, void *aBuf, ssize_t aSize, int *aFd) {
  ssize_t ret = 0;

  if (aFd) {
    *aFd = -1;

    iovec iov;
    iov.iov_base = aBuf;
    iov.iov_len = aSize;

    union {
      cmsghdr hdr;
      char control[CMSG_SPACE( sizeof(int) )];
    } cmsgu;

    msghdr msg;
    msg.msg_name = 0;
    msg.msg_namelen = 0;
    msg.msg_iov = &iov;
    msg.msg_iovlen = 1;
    msg.msg_control = cmsgu.control;
    msg.msg_controllen = sizeof(cmsgu.control);

    ret = recvmsg(aSock, &msg, 0);
    if (ret >= 0) {
      auto cmsg = CMSG_FIRSTHDR(&msg);
      if ( cmsg && cmsg->cmsg_len == CMSG_LEN( sizeof(int) ) ) {
        if (cmsg->cmsg_level != SOL_SOCKET) {
          std::cerr << "invalid cmsg_level " << cmsg->cmsg_level << std::endl;
        } else if (cmsg->cmsg_type != SCM_RIGHTS) {
          std::cerr << "invalid cmsg_type " << cmsg->cmsg_type << std::endl;
        } else {
          *aFd = *reinterpret_cast< int * >( CMSG_DATA(cmsg) );
          std::cout << "received fd " << *aFd << std::endl;
        }
      }
    }
  } else {
    ret = read(aSock, aBuf, aSize);
  }

  if (ret < 0) {
    std::cerr << "Fault to read : " << strerror(errno) << std::endl;
  }

  return ret;
}
//------------------------------------------------------------------------------
void doChild(int aSock) {
  int fd = -1;
  char msg[50] = {0};
  auto const msgSize = sizeof(msg) / sizeof(msg[0]);
  if ( -1 != sock_fd_read(aSock, msg, msgSize, &fd) ) {
    lseek(fd, 0, SEEK_SET);
    auto const readedSize = read(fd, msg, msgSize);
    if (readedSize > 0) {
      sock_fd_write(aSock, msg, readedSize, -1);
    }
    close(fd);
  }
}
//------------------------------------------------------------------------------
void doParent(int aSock) {
  std::string const msgOut("Hello, World!");

  auto fd = open("test.txt", O_CREAT | O_TRUNC | O_RDWR, 0655);
  if (-1 != fd) {
    write( fd, msgOut.c_str(), msgOut.size() );
    if ( -1 != sock_fd_write(aSock, (void *)"1", 1, fd) ) {
      char msgIn[50] = {0};
      auto const msgInSize = sizeof(msgIn) / sizeof(msgIn[0]);
      auto const readed = sock_fd_read(aSock, msgIn, msgInSize, 0);
      if (readed) {
        std::cout << "MsgOut = " << msgOut << std::endl;
        std::cout << "MsgIn = " << msgIn << std::endl;
        if (msgIn == msgOut) {
          std::cout << "Recv message IS EQUAL" << std::endl;
        } else {
          std::cerr << "Recv message IS NOT EQUAL" << std::endl;
        }
      }
    }

    close(fd);
  } else {
    std::cerr << "Fault to open : " << strerror(errno) << std::endl;
  }
}
//------------------------------------------------------------------------------
int main() {
  int socks[2] = {0, 0};

  if (socketpair(PF_LOCAL, SOCK_STREAM, 0, socks) == 0) {
    std::cout << "Press Enter to start...";
    char ch = 0;
    std::cin >> ch;

    switch( fork() ) {
    case 0:
      close(socks[0]);
      doChild(socks[1]);
      break;
    case -1:
      std::cerr << "Fault to fork : " << strerror(errno) << std::endl;
      break;
    default:
      close(socks[1]);
      doParent(socks[0]);

      std::cout << "Press Enter to stop...";
      char ch = 0;
      std::cin >> ch;

      break;
    }
  } else {
    std::cerr << "Fault to socketpair : " << strerror(errno) << std::endl;
  }

  return 0;
}
//------------------------------------------------------------------------------
