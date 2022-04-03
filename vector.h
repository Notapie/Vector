#pragma once
#include <cassert>
#include <utility>
#include <cstdlib>
#include <new>
#include <memory>

// Вспомогательный класс для управления памятью
template <typename T>
class RawMemory {
public:
    RawMemory() = default;

    explicit RawMemory(size_t capacity) : buffer_(Allocate(capacity)), capacity_(capacity) {}

    RawMemory(const RawMemory&) = delete;
    RawMemory& operator=(const RawMemory&) = delete;

    RawMemory(RawMemory&& other) noexcept {
        Swap(other);
    }

    RawMemory& operator=(RawMemory&& other) noexcept {
        if (this == &other) {
            return *this;
        }
        buffer_ = std::exchange(other.buffer_, nullptr);
        capacity_ = std::exchange(other.capacity_, 0);
        return *this;
    }

    ~RawMemory() {
        Deallocate(buffer_);
    }

    T* operator+(size_t offset) noexcept {
        // Разрешается получать адрес ячейки памяти, следующей за последним элементом массива
        assert(offset <= capacity_);
        return buffer_ + offset;
    }

    const T* operator+(size_t offset) const noexcept {
        return const_cast<RawMemory&>(*this) + offset;
    }

    const T& operator[](size_t index) const noexcept {
        return const_cast<RawMemory&>(*this)[index];
    }

    T& operator[](size_t index) noexcept {
        assert(index < capacity_);
        return buffer_[index];
    }

    void Swap(RawMemory& other) noexcept {
        std::swap(buffer_, other.buffer_);
        std::swap(capacity_, other.capacity_);
    }

    const T* GetAddress() const noexcept {
        return buffer_;
    }

    T* GetAddress() noexcept {
        return buffer_;
    }

    size_t Capacity() const {
        return capacity_;
    }

private:
    // Выделяет сырую память под n элементов и возвращает указатель на неё
    static T* Allocate(size_t n) {
        return n != 0 ? static_cast<T*>(operator new(n * sizeof(T))) : nullptr;
    }

    // Освобождает сырую память, выделенную ранее по адресу buf при помощи Allocate
    static void Deallocate(T* buf) noexcept {
        operator delete(buf);
    }

    T* buffer_ = nullptr;
    size_t capacity_ = 0;
};

// Класс самого контейнера
template <typename T>
class Vector {
public:
    using iterator = T*;
    using const_iterator = const T*;

    const_iterator cbegin() const noexcept {
        return data_.GetAddress();
    }

    const_iterator cend() const noexcept {
        return cbegin() + size_;
    }

    const_iterator begin() const noexcept {
        return cbegin();
    }

    const_iterator end() const noexcept {
        return cend();
    }

    iterator begin() noexcept {
        return const_cast<iterator>(cbegin());
    }

    iterator end() noexcept {
        return const_cast<iterator>(cend());
    }

    Vector() = default;

    explicit Vector(size_t size) : data_(size), size_(size) {
        std::uninitialized_value_construct_n(data_.GetAddress(), size_);
    }

    Vector(const Vector& other) : data_(other.size_), size_(other.size_) {
        std::uninitialized_copy_n(other.data_.GetAddress(), other.size_, data_.GetAddress());
    }

    Vector(Vector&& other) noexcept {
        data_ = std::move(other.data_);
        size_ = std::exchange(other.size_, 0);
    }

    Vector& operator=(const Vector& other) {
        if (this == &other) {
            return *this;
        }

        // Тут два варианта: когда размера выделенной памяти хватает для копирования и когда нет
        if (Capacity() >= other.size_) {
            // В своё время когда места хватает, в инициализированные объекты мы просто записываем значения
            // копируемых объектов, а оставшиеся нужно инициализировать

            // Для начала перезапишем все возможные инициализированные объекты
            size_t min_size = std::min(size_, other.size_);
            for (size_t i = 0; i < min_size; ++i) {
                (*this)[i] = other[i];
            }

            if (size_ >= other.size_) {
                // Если хватает и размера, то лишние инициализированные объекты нужно будет уничтожить
                std::destroy_n(data_.GetAddress() + other.size_, size_ - other.size_);
            } else {
                // Если нехватает, нужно инициализировать недостающее
                std::uninitialized_copy_n(
                        other.data_.GetAddress() + size_, other.size_ - size_, data_.GetAddress() + size_
                );
            }
            size_ = other.size_;
        } else {
            // Когда размера нехватает, нужно просто выделить новый участок памяти и скопировать всё туда
            // Проще всего это сделать с помощью конструктора копирования
            Vector new_vector(other);
            Swap(new_vector);
        }

        return *this;
    }

    Vector& operator=(Vector&& other) noexcept {
        if (this == &other) {
            return *this;
        }
        data_ = std::move(other.data_);
        size_ = std::exchange(other.size_, 0);
        return *this;
    }

    void PushBack(const T& value) {
        EmplaceBack(value);
    }

    void PushBack(T&& rhs_value) {
        EmplaceBack(std::move(rhs_value));
    }

    template <typename... Args>
    T& EmplaceBack(Args&&... args) {
        return *Emplace(end(), std::forward<Args&&>(args)...);
    }

    iterator Insert(const_iterator pos, const T& value) {
        return Emplace(pos, value);
    }

    iterator Insert(const_iterator pos, T&& value) {
        return Emplace(pos, std::move(value));
    }

    template<typename... Args>
    iterator Emplace(const_iterator pos, Args&&... args) {
        size_t index = pos - begin();

        // Тут два варианта: хватает капасити или нет
        // Разница лишь в том, что если капасити нехватает, то мы конструируем объект сразу в новом участке памяти
        if (size_ < Capacity()) {
            if (pos == end()) {
                new (data_.GetAddress() + size_) T(std::forward<Args&&>(args)...);
            } else {
                T temp_object(std::forward<Args&&>(args)...);
                new (data_.GetAddress() + size_) T(std::move(*(end() - 1)));
                std::move_backward(begin() + index, end() - 1, end());
                *(begin() + index) = std::move(temp_object);
            }
        } else {
            RawMemory<T> new_data(size_ == 0 ? 1 : size_ * 2);
            new (new_data.GetAddress() + index) T(std::forward<Args&&>(args)...);

            // Тут при исключении нужно разрушить один первый сконструированный объект в новой памяти
            try {
                MoveData(begin(), index, new_data.GetAddress());
            } catch (...) {
                std::destroy_n(new_data.GetAddress() + index, 1);
                throw;
            }

            // Тут при исключении нужно разрушить все элементы с начала и до вставляемого включительно
            try {
                MoveData(begin() + index, size_ - index, new_data.GetAddress() + index + 1);
            } catch (...) {
                std::destroy_n(new_data.GetAddress(), index + 1);
                throw;
            }

            std::destroy_n(data_.GetAddress(), size_);
            data_.Swap(new_data);
        }

        ++size_;
        return begin() + index;
    }

    iterator Erase(const_iterator pos) {
        size_t index = pos - begin();

        std::move(begin() + index + 1, end(), begin() + index);
        std::destroy_n(end() - 1, 1);

        --size_;
        return begin() + index;
    }

    void Resize(size_t new_size) {
        if (new_size == size_) {
            return;
        } else if (new_size > Capacity()) {
            // Если новый размер больше, чем вместимость, нужно выделить новый кусок памяти, переместить туда все элементы,
            // а разницу между новым и старым size заполнить объектами по умолчанию
            RawMemory<T> new_data(new_size);
            std::uninitialized_value_construct_n(new_data.GetAddress() + size_, new_size - size_);
            MoveToNewData(new_data);

        } else {
            // Если ноый размер меньше или равен вместимости, есть два варианта:
            if (size_ > new_size) {
                // 1 - новый размер меньше, чем старый. Нужно просто разницу старого и нового размеров разрушить
                std::destroy_n(data_.GetAddress() + new_size, size_ - new_size);
            } else {
                // 2 - новый размер больше. Нужно разницу нового и старого инициализировать
                std::uninitialized_value_construct_n(data_.GetAddress() + size_, new_size - size_);
            }
        }
        size_ = new_size;
    }

    void PopBack() noexcept {
        Resize(size_ - 1);
    }

    size_t Size() const noexcept {
        return size_;
    }

    size_t Capacity() const noexcept {
        return data_.Capacity();
    }

    const T& operator[](size_t index) const noexcept {
        return const_cast<Vector&>(*this)[index];
    }

    T& operator[](size_t index) noexcept {
        assert(index < size_);
        return data_[index];
    }

    ~Vector() {
        std::destroy_n(data_.GetAddress(), size_);
    }

    void Reserve(size_t new_capacity) {
        if (new_capacity <= Capacity()) {
            return;
        }
        // Выбираем, перемещать или копировать объекты в новую область памяти в зависимости от наличия нужных
        // конструкторов
        RawMemory<T> new_data(new_capacity);
        MoveToNewData(new_data);
    }

    void Swap(Vector& other) noexcept {
        data_.Swap(other.data_);
        std::swap(size_, other.size_);
    }

private:
    RawMemory<T> data_;
    size_t size_ = 0;

    // Тут нужно подумать над названием
    void MoveToNewData(RawMemory<T>& new_data) {
        // Сначала определяем, как именно будет происходить перемещение и перемещаем
        if constexpr (std::is_nothrow_move_constructible_v<T> || !std::is_copy_constructible_v<T>) {
            std::uninitialized_move_n(data_.GetAddress(), size_, new_data.GetAddress());
        } else {
            std::uninitialized_copy_n(data_.GetAddress(), size_, new_data.GetAddress());
        }
        // Тут просто уничтожаем старые объекты и заменяем область памяти на новую
        std::destroy_n(data_.GetAddress(), size_);
        data_.Swap(new_data);
    }

    void MoveData(iterator src, size_t size, iterator dest) {
        // Сначала определяем, как именно будет происходить перемещение и перемещаем
        if constexpr (std::is_nothrow_move_constructible_v<T> || !std::is_copy_constructible_v<T>) {
            std::uninitialized_move_n(src, size, dest);
        } else {
            std::uninitialized_copy_n(src, size, dest);
        }
    }
};
