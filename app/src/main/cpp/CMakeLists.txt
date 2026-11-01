cmake_minimum_required(VERSION 3.12)

project(CaspianProject LANGUAGES C CXX)

# Harici bağımlılıklar
find_package(shadowhook REQUIRED CONFIG)

# Alt dizinler
add_subdirectory(opus)
add_subdirectory(samp)

# 1. Global include_directories yerine, Opus veya Samp'in ihtiyacına göre hedef bazlı ekleme yapın
# (Genellikle Opus kütüphanesinin kendi CMakeLists.txt dosyasıinclude dizinlerini zaten ayarlar)
target_include_directories(opus PRIVATE
    ${CMAKE_CURRENT_LIST_DIR}/opus/include
)

# 2. Optimizasyon bayrağı (Release modunda CMake zaten -O2 / -O3 uygular ancak manuel eklemek güvenlidir)
target_compile_options(opus PRIVATE -O3)

# 3. Eğer samp kütüphanesi shadowhook kullanıyorsa, burada linklenmesi gerekebilir:
# target_link_libraries(samp PRIVATE shadowhook::shadowhook)
