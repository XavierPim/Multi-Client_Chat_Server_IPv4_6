# COMP4985-Project

By:   Randall Kong, Roy Xavier Pimentel
Role: Team 3 - Server

# Iso Instructions:
1) ./generate-flags.sh
2) ./generate-cmakelists.sh
3) ./change-compiler.sh -c [gcc or clang]
4) ./build.sh

#GCC 
gcc -Iinclude src/server.c src/wrapper.c -o server
gcc -Iinclude src/client.c -o client


# Run
1) ./server [ip address] [port]
2) ./client [ip address] [port]

# Tips
- don't push files .sh executables generate.

# Testing
- connection with client                   {Status - GOOD}
- reliable read/write                      {Status - GOOD}
- works with gcc and clang                 {Status - GOOD}
- multiple concurrent connections          {Status - GOOD}
- commands (/h, /ul, /u, /w)               {Status - Not Yet}
- work with all os (MacOS, Manjaro, ??)    {Status - Not Yet}

- with client team 6 (Jiang Peng, Jianhua) {Status - Not Yet}
- with client team 7 (Dong-il, Tushar)     {Status - Not Yet}
- with client team 8 (Kiefer, Jack)        {Status - Not Yet}

