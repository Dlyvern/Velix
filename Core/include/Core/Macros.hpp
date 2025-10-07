//* Yeah, I know that store Macroses inside Core Layer is not ideal...

#ifndef ELIX_MACROS_HPP
#define ELIX_MACROS_HPP

#define ELIX_NAMESPACE_BEGIN namespace elix {
#define ELIX_NAMESPACE_END }

#define ELIX_NESTED_NAMESPACE_BEGIN(x) namespace elix { namespace x {
#define ELIX_NESTED_NAMESPACE_END }} 

#endif //ELIX_MACROS_HPP