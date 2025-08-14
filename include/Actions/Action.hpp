#ifndef ACTION_HPP
#define ACTION_HPP

class Action
{
public:
    virtual void execute() = 0;

    virtual void undo() = 0;

    virtual void redo() = 0;

    virtual ~Action() = default;
};

#endif //ACTION_HPP
