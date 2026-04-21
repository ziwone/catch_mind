cmake_minimum_required(VERSION 3.10)
project(CatchMind)

# 커널 버전 확인 (Linux 6.1.63)
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# 공통 소스 파일
set(COMMON_SOURCES
	src/main.cpp
	src/game.cpp
	src/bgm.cpp
)

# 기본 실행 파일 생성 (framebuffer backend)
add_executable(catch_mind ${COMMON_SOURCES} src/display.cpp)

# 헤더 파일 경로 설정
target_include_directories(catch_mind PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/include)

# 필요한 라이브러리 링크 (aplay는 외부 프로세스로 실행하므로 asound/mpg123 불필요)
target_link_libraries(catch_mind pthread)

# 설치 경로
install(TARGETS catch_mind DESTINATION bin)
