/*

Name:   catena-message-port 4-test.cpp

Function:
    Generate test vectors for port 0x04 messages.

Copyright and License:
    See accompanying LICENSE file at https://github.com/mcci-catena/MCCI-Catena-4430/

Author:
    Terry Moore, MCCI Corporation   August 2020

*/

#include <cmath>
#include <cstdint>
#include <iostream>
#include <string>
#include <vector>


template <typename T>
struct val
    {
    bool fValid;
    T v;
    };

struct env
    {
    float t;
    float rh;
    };

struct modbus
    {
    static constexpr unsigned knReg = (240 - 13) / 2;
    unsigned nReg;
    float Register[knReg];
    };

struct Measurements
    {
    val<float> Vbat;
    val<float> Vsys;
    val<std::uint8_t> Boot;
    val<env> Env;
    val<modbus> Modbus;
    };

uint16_t
LMIC_f2uflt16(
        float f
        )
        {
        if (f < 0.0)
                return 0;
        else if (f >= 1.0)
                return 0xFFFF;
        else
                {
                int iExp;
                float normalValue;

                normalValue = std::frexp(f, &iExp);

                // f is supposed to be in [0..1), so useful exp
                // is [0..-15]
                iExp += 15;
                if (iExp < 0)
                        // underflow.
                        iExp = 0;

                // bits 15..12 are the exponent
                // bits 11..0 are the fraction
                // we conmpute the fraction and then decide if we need to round.
                uint16_t outputFraction = std::ldexp(normalValue, 12) + 0.5;
                if (outputFraction >= (1 << 12u))
                        {
                        // reduce output fraction
                        outputFraction = 1 << 11;
                        // increase exponent
                        ++iExp;
                        }

                // check for overflow and return max instead.
                if (iExp > 15)
                        return 0xFFFF;

                return (uint16_t)((iExp << 12u) | outputFraction);
                }
        }

/*

Name:   LMIC_f2sflt16()

Function:
        Encode a floating point number into a uint16_t.

Definition:
        uint16_t LMIC_f2sflt16(
                float f
                );

Description:
        The float to be transmitted must be a number in the range (-1.0, 1.0).
        It is converted to 16-bit integer formatted as follows:

                bits 15: sign
                bits 14..11: biased exponent
                bits 10..0: mantissa

        The float is properly rounded, and saturates.

        Note that the encoded value is sign/magnitude format, rather than
        two's complement for negative values.

Returns:
        0xFFFF for negative values <= 1.0;
        0x7FFF for positive values >= 1.0;
        Otherwise an appropriate float.

*/

uint16_t
LMIC_f2sflt16(
        float f
        )
        {
        if (f <= -1.0)
                return 0xFFFF;
        else if (f >= 1.0)
                return 0x7FFF;
        else
                {
                int iExp;
                float normalValue;
                uint16_t sign;

                normalValue = frexpf(f, &iExp);

                sign = 0;
                if (normalValue < 0)
                        {
                        // set the "sign bit" of the result
                        // and work with the absolute value of normalValue.
                        sign = 0x8000;
                        normalValue = -normalValue;
                        }

                // abs(f) is supposed to be in [0..1), so useful exp
                // is [0..-15]
                iExp += 15;
                if (iExp < 0)
                        iExp = 0;

                // bit 15 is the sign
                // bits 14..11 are the exponent
                // bits 10..0 are the fraction
                // we conmpute the fraction and then decide if we need to round.
                uint16_t outputFraction = ldexpf(normalValue, 11) + 0.5;
                if (outputFraction >= (1 << 11u))
                        {
                        // reduce output fraction
                        outputFraction = 1 << 10;
                        // increase exponent
                        ++iExp;
                        }

                // check for overflow and return max instead.
                if (iExp > 15)
                        return 0x7FFF | sign;

                return (uint16_t)(sign | (iExp << 11u) | outputFraction);
                }
        }

std::uint16_t encode16s(float v)
    {
    float nv = std::floor(v + 0.5f);

    if (nv > 32767.0f)
        return 0x7FFFu;
    else if (nv < -32768.0f)
        return 0x8000u;
    else
        {
        return (std::uint16_t) std::int16_t(nv);
        }
    }

std::uint16_t encode16u(float v)
    {
    float nv = std::floor(v + 0.5f);
    if (nv > 65535.0f)
        return 0xFFFFu;
    else if (nv < 0.0f)
        return 0;
    else
        {
        return std::uint16_t(nv);
        }
    }

std::uint16_t encodeV(float v)
    {
    return encode16s(v * 4096.0f);
    }

std::uint16_t encodeT(float v)
    {
    return encode16s(v * 256.0f);
    }

std::uint16_t encodeRH(float v)
    {
    return encode16u(v * 65535.0f / 100.0f);
    }

std::uint16_t encodeRegister(float v)
    {
    return encode16u(v);
    }

class Buffer : public std::vector<std::uint8_t>
    {
public:
    Buffer() : std::vector<std::uint8_t>() {};

    void push_back_be(std::uint16_t v)
        {
        this->push_back(std::uint8_t(v >> 8));
        this->push_back(std::uint8_t(v & 0xFF));
        }

    void push_back_be4(std::uint32_t v)
        {
        this->push_back(std::uint8_t(v >> 24));
        this->push_back(std::uint8_t(v >> 16));
        this->push_back(std::uint8_t(v >> 8));
        this->push_back(std::uint8_t(v & 0xFF));
        }
    };

void encodeMeasurement(Buffer &buf, Measurements &m)
    {
    std::uint8_t flags = 0;

    buf.clear();

    // send the flag byte
    buf.push_back(0u); // flag byte.
    auto const iFlags = buf.size() - 1;

    // put the fields
    if (m.Vbat.fValid)
        {
        flags |= 1 << 0;
        buf.push_back_be(encodeV(m.Vbat.v));
        }

    if (m.Vsys.fValid)
        {
        flags |= 1 << 1;
        buf.push_back_be(encodeV(m.Vsys.v));
        }

    if (m.Boot.fValid)
        {
        flags |= 1 << 2;
        buf.push_back(m.Boot.v);
        }

    if (m.Env.fValid)
        {
        flags |= 1 << 3;

        buf.push_back_be(encodeT(m.Env.v.t));
        buf.push_back_be(encodeRH(m.Env.v.rh));
        }

    if (m.Modbus.fValid)
        {
        for (unsigned i = 0; i < m.Modbus.v.nReg; ++i)
            {
            buf.push_back_be(encodeRegister(m.Modbus.v.Register[i]));
            }
        }

    // update the flags
    buf.data()[iFlags] = flags;
    }

void logMeasurement(Measurements &m)
    {
    class Padder {
    public:
        Padder() : m_first(true) {}
        const char *get() {
            if (this->m_first)
                {
                this->m_first = false;
                return "";
                }
            else
                return " ";
            }
        const char *nl() {
            return this->m_first ? "" : "\n";
            }
    private:
        bool m_first;
    } pad;

    if (m.Vbat.fValid)
        {
        std::cout << pad.get() << "Vbat " << m.Vbat.v;
        }

    if (m.Vsys.fValid)
        {
        std::cout << pad.get() << "Vsys " << m.Vsys.v;
        }

    if (m.Boot.fValid)
        {
        std::cout << pad.get() << "Boot " << unsigned(m.Boot.v);
        }

    if (m.Env.fValid)
        {
        std::cout << pad.get() << "Env " << m.Env.v.t << " "
                                         << m.Env.v.rh;
        }

    if (m.Modbus.fValid)
        {
        std::cout << pad.get() << "Modbus [";

        for (unsigned i = 0; i < m.Modbus.v.nReg; ++i)
            std::cout << " " << m.Modbus.v.Register[i];

        std::cout << " ]";
        }

    // make the syntax cut/pastable.
    std::cout << pad.get() << ".\n";
    }

void putTestVector(Measurements &m)
    {
    Buffer buf {};
    logMeasurement(m);
    encodeMeasurement(buf, m);
    bool fFirst;

    fFirst = true;
    for (auto v : buf)
        {
        if (! fFirst)
            std::cout << ' ';
        fFirst = false;
        std::cout.width(2);
        std::cout.fill('0');
        std::cout << std::hex << unsigned(v);
        }
    std::cout << '\n';
    std::cout << "length: " << std::dec << buf.end() - buf.begin() << '\n';
    }

int main(int argc, char **argv)
    {
    Measurements m {0};
    Measurements m0 {0};
    bool fAny;
    std::string key;

    std::cout << "Input one or more lines of name/value tuples, ended by '.'\n";

    fAny = false;
    while (std::cin.good())
        {
        bool fUpdate = true;
        key.clear();

        std::cin >> key;

        if (key == "Vbat")
            {
            std::cin >> m.Vbat.v;
            m.Vbat.fValid = true;
            }
        else if (key == "Vsys")
            {
            std::cin >> m.Vsys.v;
            m.Vsys.fValid = true;
            }
        else if (key == "Boot")
            {
            std::uint32_t nonce;
            std::cin >> nonce;
            m.Boot.v = (std::uint8_t) nonce;
            m.Boot.fValid = true;
            }
        else if (key == "Env")
            {
            std::cin >> m.Env.v.t >> m.Env.v.rh;
            m.Env.fValid = true;
            }
        else if (key == "Modbus")
            {
            std::string token;
            std::cin >> token;
            if (token != "[")
                {
                std::cerr << "Modbus parse error: expected '[': " << token << "\n";
                return 1;
                }

            unsigned i = 0;
            for (;i < std::size(m.Modbus.v.Register); ++i)
                {
                // read a word.
                std::cin.clear();
                std::cin >> m.Modbus.v.Register[i];

                auto const state = std::cin.rdstate();

                if (state & (std::cin.eofbit | std::cin.failbit | std::cin.badbit))
                    {
                    std::cin.clear(state & ~std::cin.failbit);
                    break;
                    }
                }

            m.Modbus.fValid = true;
            m.Modbus.v.nReg = i;

            std::cin >> token;
            if (token != "]")
                {
                std::cerr << "Modbus parse error: expected ']': " << token << "\n";
                return 1;
                }

            }
        else if (key == ".")
            {
            putTestVector(m);
            m = m0;
            fAny = false;
            fUpdate = false;
            }
        else if (key == "")
            /* ignore empty keys */
            fUpdate = false;
        else
            {
            std::cerr << "unknown key: " << key << "\n";
            fUpdate = false;
            }

        fAny |= fUpdate;
        }

    if (!std::cin.eof() && std::cin.fail())
        {
        std::string nextword;

        std::cin.clear(std::cin.goodbit);
        std::cin >> nextword;
        std::cerr << "parse error: " << nextword << "\n";
        return 1;
        }

    if (fAny)
        putTestVector(m);

    return 0;
    }
