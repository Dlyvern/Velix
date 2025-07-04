#ifndef IINSPECTABLE_HPP
#define IINSPECTABLE_HPP

class IInspectable
{
public:
    virtual ~IInspectable() = default;
    virtual void draw() = 0;
};

#endif //IINSPECTABLE_HPP