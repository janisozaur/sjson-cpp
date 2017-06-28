#pragma once

////////////////////////////////////////////////////////////////////////////////
// The MIT License (MIT)
//
// Copyright (c) 2017 Nicholas Frechette, Cody Jones, and sjson-cpp contributors
//
// Permission is hereby granted, free of charge, to any person obtaining a copy
// of this software and associated documentation files (the "Software"), to deal
// in the Software without restriction, including without limitation the rights
// to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
// copies of the Software, and to permit persons to whom the Software is
// furnished to do so, subject to the following conditions:
//
// The above copyright notice and this permission notice shall be included in all
// copies or substantial portions of the Software.
//
// THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
// IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
// FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
// AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
// LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
// OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
// SOFTWARE.
////////////////////////////////////////////////////////////////////////////////

#include "parser_error.h"
#include "parser_state.h"
#include "string_view.h"

#include <cctype>
#include <cstdlib>
#include <cstring>
#include <stdint.h>

namespace sjson
{
	class Parser
	{
	public:
		Parser(const char* input, size_t input_length)
			: m_input(input)
			, m_input_length(input_length)
			, m_state(input, input_length)
		{
		}

		bool object_begins() { return read_opening_brace(); }
		bool object_begins(const char* having_name) { return read_key(having_name) && read_equal_sign() && object_begins(); }
		bool object_ends() { return read_closing_brace(); }

		bool array_begins() { return read_opening_bracket(); }
		bool array_begins(const char* having_name) { return read_key(having_name) && read_equal_sign() && read_opening_bracket(); }
		bool array_ends() { return read_closing_bracket(); }

		bool try_array_begins(const char* having_name)
		{
			ParserState s = save_state();

			if (!array_begins(having_name))
			{
				restore_state(s);
				return false;
			}

			return true;
		}

		bool try_array_ends() 
		{
			ParserState s = save_state();

			if (!array_ends())
			{
				restore_state(s);
				return false;
			}

			return true;
		}

		bool read(const char* key, StringView& value) { return read_key(key) && read_equal_sign() && read_string(value); }
		bool read(const char* key, bool& value) { return read_key(key) && read_equal_sign() && read_bool(value); }
		bool read(const char* key, double& value) { return read_key(key) && read_equal_sign() && read_double(value); }

		bool read(const char* key, double* values, uint32_t num_elements)
		{
			return read_key(key) && read_equal_sign() && read_opening_bracket() && read(values, num_elements) && read_closing_bracket();
		}

		bool read(double* values, uint32_t num_elements)
		{
			if (num_elements == 0)
				return true;

			for (uint32_t i = 0; i < num_elements; ++i)
			{
				if (!read_double(values[i]) || i < num_elements - 1 && !read_comma())
					return false;
			}

			return true;
		}

		bool try_read(const char* key, StringView& value)
		{
			ParserState s = save_state();

			if (!read(key, value))
			{
				restore_state(s);
				value = nullptr;
				return false;
			}

			return true;
		}

		bool try_read(const char* key, double* values, uint32_t num_elements)
		{
			ParserState s = save_state();
			
			if (!read(key, values, num_elements))
			{
				restore_state(s);

				for (uint32_t i = 0; i < num_elements; ++i)
					values[i] = 0.0;

				return false;
			}

			return true;
		}

		bool remainder_is_comments_and_whitespace()
		{
			if (!skip_comments_and_whitespace())
				return false;

			if (!eof())
			{
				set_error(ParserError::UnexpectedContentAtEnd);
				return false;
			}

			return true;
		}

		bool skip_comments_and_whitespace()
		{
			while (true)
			{
				if (eof())
					return true;

				if (std::isspace(m_state.symbol))
				{
					advance();
					continue;
				}

				if (m_state.symbol == '/')
				{
					advance();

					if (!read_comment())
						return false;
				}

				return true;
			}
		}

		void get_position(uint32_t& line, uint32_t& column)
		{
			line = m_state.line;
			column = m_state.column;
		}

		bool eof() { return m_state.offset >= m_input_length; }

		ParserError get_error() { return m_state.error; }

		ParserState save_state() const { return m_state; }
		void restore_state(const ParserState& s) { m_state = s; }
		void reset_state() { m_state = ParserState(m_input, m_input_length); }

	private:
		static size_t constexpr MAX_NUMBER_LENGTH = 64;

		const char* m_input;
		const size_t m_input_length;
		ParserState m_state;

		bool read_equal_sign()		{ return read_symbol('=', ParserError::EqualSignExpected); }
		bool read_opening_brace()	{ return read_symbol('{', ParserError::OpeningBraceExpected); }
		bool read_closing_brace()	{ return read_symbol('}', ParserError::ClosingBraceExpected); }
		bool read_opening_bracket()	{ return read_symbol('[', ParserError::OpeningBracketExpected); }
		bool read_closing_bracket()	{ return read_symbol(']', ParserError::ClosingBracketExpected); }
		bool read_comma()			{ return read_symbol(',', ParserError::CommaExpected); }

		bool read_symbol(char expected, int32_t reason_if_other_found)
		{
			if (!skip_comments_and_whitespace_fail_if_eof())
				return false;

			if (m_state.symbol == expected)
			{
				advance();
				return true;
			}

			set_error(reason_if_other_found);
			return false;
		}

		bool read_comment()
		{
			if (eof())
			{
				set_error(ParserError::InputTruncated);
				return false;
			}

			if (m_state.symbol == '/')
			{
				while (!eof() && m_state.symbol != '\n')
					advance();

				return true;
			}
			else if (m_state.symbol == '*')
			{
				advance();

				bool wasAsterisk = false;

				while (true)
				{
					if (eof())
					{
						set_error(ParserError::InputTruncated);
						return false;
					}
					else if (m_state.symbol == '*')
					{
						advance();
						wasAsterisk = true;
					}
					else if (wasAsterisk && m_state.symbol == '/')
					{
						advance();
						return true;
					}
					else
					{
						advance();
						wasAsterisk = false;
					}
				}
			}
			else
			{
				set_error(ParserError::CommentBeginsIncorrectly);
				return false;
			}
		}

		bool read_key(const char* having_name)
		{
			if (!skip_comments_and_whitespace_fail_if_eof())
				return false;

			ParserState start_of_key = save_state();
			StringView actual;

			if (m_state.symbol == '"')
			{
				if (!read_string(actual))
					return false;
			}
			else
			{
				if (!read_unquoted_key(actual))
					return false;
			}

			if (actual != having_name)
			{
				restore_state(start_of_key);
				set_error(ParserError::IncorrectKey);
				return false;
			}

			return true;
		}

		bool read_string(StringView& value)
		{
			if (!skip_comments_and_whitespace_fail_if_eof())
				return false;

			if (m_state.symbol != '"')
			{
				set_error(ParserError::QuotationMarkExpected);
				return false;
			}

			advance();

			size_t start_offset = m_state.offset;
			size_t end_offset;

			while (true)
			{
				if (eof())
				{
					set_error(ParserError::InputTruncated);
					return false;
				}

				if (m_state.symbol == '"')
				{
					end_offset = m_state.offset - 1;
					advance();
					break;
				}

				if (m_state.symbol == '\\')
				{
					// Strings are returned as slices of the input, so escape sequences cannot be un-escaped.
					// Assume the escape sequence is valid and skip over it.
					advance();
				}

				advance();
			}

			value = StringView(m_input + start_offset, end_offset - start_offset + 1);
			return true;
		}

		bool read_unquoted_key(StringView& value)
		{
			if (eof())
			{
				set_error(ParserError::InputTruncated);
				return false;
			}

			size_t start_offset = m_state.offset;
			size_t end_offset;

			while (true)
			{
				if (eof())
				{
					end_offset = m_state.offset - 1;
					break;
				}

				if (m_state.symbol == '"')
				{
					set_error(ParserError::CannotUseQuotationMarkInUnquotedString);
					return false;
				}

				if (m_state.symbol == '=')
				{
					if (m_state.offset == start_offset)
					{
						set_error(ParserError::KeyExpected);
						return false;
					}

					end_offset = m_state.offset - 1;
					break;
				}

				if (std::isspace(m_state.symbol))
				{
					end_offset = m_state.offset - 1;
					advance();
					break;
				}

				advance();
			}

			value = StringView(m_input + start_offset, end_offset - start_offset + 1);
			return true;
		}

		bool read_bool(bool& value)
		{
			if (!skip_comments_and_whitespace_fail_if_eof())
				return false;

			ParserState start_of_literal = save_state();

			if (m_state.symbol == 't')
			{
				advance();

				if (m_state.symbol == 'r' && advance() &&
					m_state.symbol == 'u' && advance() &&
					m_state.symbol == 'e' && advance())
				{
					value = true;
					return true;
				}
			}
			else if (m_state.symbol == 'f')
			{
				advance();

				if (m_state.symbol == 'a' && advance() &&
					m_state.symbol == 'l' && advance() &&
					m_state.symbol == 's' && advance() &&
					m_state.symbol == 'e' && advance())
				{
					value = false;
					return true;
				}
			}
			
			restore_state(start_of_literal);
			set_error(ParserError::TrueOrFalseExpected);
			return false;
		}

		bool read_double(double& value)
		{
			if (!skip_comments_and_whitespace_fail_if_eof())
				return false;

			size_t start_offset = m_state.offset;
			size_t end_offset;

			if (m_state.symbol == '-')
				advance();

			if (m_state.symbol == '0')
			{
				advance();
			}
			else if (std::isdigit(m_state.symbol))
			{
				while (std::isdigit(m_state.symbol))
					advance();
			}
			else
			{
				set_error(ParserError::NumberExpected);
				return false;
			}

			if (m_state.symbol == '.')
			{
				advance();

				while (std::isdigit(m_state.symbol))
					advance();
			}

			if (m_state.symbol == 'e' || m_state.symbol == 'E')
			{
				advance();

				if (m_state.symbol == '+' || m_state.symbol == '-')
				{
					advance();
				}
				else if (!std::isdigit(m_state.symbol))
				{
					set_error(ParserError::InvalidNumber);
					return false;
				}

				while (std::isdigit(m_state.symbol))
					advance();
			}
			
			end_offset = m_state.offset - 1;
			size_t length = end_offset - start_offset + 1;

			char slice[MAX_NUMBER_LENGTH + 1];
			if (length >= MAX_NUMBER_LENGTH)
			{
				set_error(ParserError::NumberIsTooLong);
				return false;
			}

			std::memcpy(slice, m_input + start_offset, length);
			slice[length] = '\0';

			char* last_used_symbol = nullptr;
			value = std::strtod(slice, &last_used_symbol);

			if (last_used_symbol != slice + length)
			{
				set_error(ParserError::NumberCouldNotBeConverted);
				return false;
			}

			return true;
		}

		bool skip_comments_and_whitespace_fail_if_eof()
		{
			if (!skip_comments_and_whitespace())
				return false;

			if (eof())
			{
				set_error(ParserError::InputTruncated);
				return false;
			}

			return true;
		}

		bool advance()
		{
			if (eof())
				return false;

			m_state.offset++;

			if (eof())
			{
				m_state.symbol = '\0';
			}
			else
			{
				m_state.symbol = m_input[m_state.offset];

				if (m_state.symbol == '\n')
				{
					++m_state.line;
					m_state.column = 1;
				}
				else
				{
					m_state.column++;
				}
			}

			return true;
		}

		void set_error(int32_t error)
		{
			m_state.error.error = error;
			m_state.error.line = m_state.line;
			m_state.error.column = m_state.column;
		}
	};
}