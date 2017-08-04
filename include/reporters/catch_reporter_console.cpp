/*
 *  Created by Phil on 5/12/2012.
 *  Copyright 2012 Two Blue Cubes Ltd. All rights reserved.
 *
 *  Distributed under the Boost Software License, Version 1.0. (See accompanying
 *  file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
 */

#include "catch_reporter_bases.hpp"

#include "../internal/catch_reporter_registrars.hpp"
#include "../internal/catch_console_colour.hpp"
#include "../internal/catch_version.h"
#include "../internal/catch_text.h"

#include <cfloat>
#include <cstdio>

namespace {
    std::size_t makeRatio( std::size_t number, std::size_t total ) {
        std::size_t ratio = total > 0 ? CATCH_CONFIG_CONSOLE_WIDTH * number/ total : 0;
        return ( ratio == 0 && number > 0 ) ? 1 : ratio;
    }

    std::size_t& findMax( std::size_t& i, std::size_t& j, std::size_t& k ) {
        if( i > j && i > k )
            return i;
        else if( j > k )
            return j;
        else
            return k;
    }
}

namespace Catch {

    template<typename T>
    auto leftPad( const T& value, int width ) -> std::string {
        // !TBD: could do with being optimised
        std::ostringstream oss;
        oss << value;
        std::string converted = oss.str();
        return std::string( width - converted.size(), ' ' ) + converted;
    }


    struct ConsoleReporter : StreamingReporterBase<ConsoleReporter> {
        using StreamingReporterBase::StreamingReporterBase;
        bool m_benchmarkTableOpen = false;

        ~ConsoleReporter() override;
        static std::string getDescription() {
            return "Reports test results as plain lines of text";
        }

        void noMatchingTestCases( std::string const& spec ) override {
            stream << "No test cases matched '" << spec << '\'' << std::endl;
        }

        void assertionStarting( AssertionInfo const& ) override {
            closeBenchmarkTable();
        }

        bool assertionEnded( AssertionStats const& _assertionStats ) override {
            AssertionResult const& result = _assertionStats.assertionResult;

            bool includeResults = m_config->includeSuccessfulResults() || !result.isOk();

            // Drop out if result was successful but we're not printing them.
            if( !includeResults && result.getResultType() != ResultWas::Warning )
                return false;

            lazyPrint();

            AssertionPrinter printer( stream, _assertionStats, includeResults );
            printer.print();
            stream << std::endl;
            return true;
        }

        void sectionStarting( SectionInfo const& _sectionInfo ) override {
            m_headerPrinted = false;
            StreamingReporterBase::sectionStarting( _sectionInfo );
        }
        void sectionEnded( SectionStats const& _sectionStats ) override {
            closeBenchmarkTable();
            if( _sectionStats.missingAssertions ) {
                lazyPrint();
                Colour colour( Colour::ResultError );
                if( m_sectionStack.size() > 1 )
                    stream << "\nNo assertions in section";
                else
                    stream << "\nNo assertions in test case";
                stream << " '" << _sectionStats.sectionInfo.name << "'\n" << std::endl;
            }
            if( m_config->showDurations() == ShowDurations::Always ) {
                stream << getFormattedDuration(_sectionStats.durationInSeconds) << " s: " << _sectionStats.sectionInfo.name << std::endl;
            }
            if( m_headerPrinted ) {
                m_headerPrinted = false;
            }
            StreamingReporterBase::sectionEnded( _sectionStats );
        }

        void benchmarkStarting( BenchmarkInfo const& info ) override {
            lazyPrint();
            auto nameColWidth = CATCH_CONFIG_CONSOLE_WIDTH-40;
            auto nameCol = Column( info.name ).width( nameColWidth );
            if( !m_benchmarkTableOpen ) {
                stream
                    << getBoxCharsAcross() << "\n"
                    << "| benchmark name" << std::string( nameColWidth-14, ' ' ) << " |  it'ns | elapsed ns |    average |\n"
                    << getBoxCharsAcross() << "\n";
                m_benchmarkTableOpen = true;
            }
            bool firstLine = true;
            for( auto line : nameCol ) {
                if( !firstLine )
                    stream << "        |            |            |" << "\n";
                else
                    firstLine = false;

                stream << "| " << line << std::string( nameColWidth-line.size(), ' ' ) << " |";
            }
        }
        void benchmarkEnded( BenchmarkStats const& stats ) override {
            // !TBD: report average times in natural units?
            stream
                << " " << leftPad( stats.iterations, 6 )
                << " | " << leftPad( stats.elapsedTimeInNanoseconds, 10 )
                << " | " << leftPad( stats.elapsedTimeInNanoseconds/stats.iterations, 7 )
                << " ns |" << std::endl;
        }
        void closeBenchmarkTable() {
            if( m_benchmarkTableOpen ) {
                stream << getBoxCharsAcross() << "\n" << std::endl;
                m_benchmarkTableOpen = false;
            }
        }

        void testCaseEnded( TestCaseStats const& _testCaseStats ) override {
            closeBenchmarkTable();
            StreamingReporterBase::testCaseEnded( _testCaseStats );
            m_headerPrinted = false;
        }
        void testGroupEnded( TestGroupStats const& _testGroupStats ) override {
            if( currentGroupInfo.used ) {
                printSummaryDivider();
                stream << "Summary for group '" << _testGroupStats.groupInfo.name << "':\n";
                printTotals( _testGroupStats.totals );
                stream << '\n' << std::endl;
            }
            StreamingReporterBase::testGroupEnded( _testGroupStats );
        }
        void testRunEnded( TestRunStats const& _testRunStats ) override {
            printTotalsDivider( _testRunStats.totals );
            printTotals( _testRunStats.totals );
            stream << std::endl;
            StreamingReporterBase::testRunEnded( _testRunStats );
        }

    private:

        class AssertionPrinter {
        public:
            AssertionPrinter& operator= ( AssertionPrinter const& ) = delete;
            AssertionPrinter( AssertionPrinter const& ) = delete;
            AssertionPrinter( std::ostream& _stream, AssertionStats const& _stats, bool _printInfoMessages )
            :   stream( _stream ),
                stats( _stats ),
                result( _stats.assertionResult ),
                colour( Colour::None ),
                message( result.getMessage() ),
                messages( _stats.infoMessages ),
                printInfoMessages( _printInfoMessages )
            {
                switch( result.getResultType() ) {
                    case ResultWas::Ok:
                        colour = Colour::Success;
                        passOrFail = "PASSED";
                        //if( result.hasMessage() )
                        if( _stats.infoMessages.size() == 1 )
                            messageLabel = "with message";
                        if( _stats.infoMessages.size() > 1 )
                            messageLabel = "with messages";
                        break;
                    case ResultWas::ExpressionFailed:
                        if( result.isOk() ) {
                            colour = Colour::Success;
                            passOrFail = "FAILED - but was ok";
                        }
                        else {
                            colour = Colour::Error;
                            passOrFail = "FAILED";
                        }
                        if( _stats.infoMessages.size() == 1 )
                            messageLabel = "with message";
                        if( _stats.infoMessages.size() > 1 )
                            messageLabel = "with messages";
                        break;
                    case ResultWas::ThrewException:
                        colour = Colour::Error;
                        passOrFail = "FAILED";
                        messageLabel = "due to unexpected exception with ";
                        if (_stats.infoMessages.size() == 1)
                            messageLabel += "message";
                        if (_stats.infoMessages.size() > 1)
                            messageLabel += "messages";
                        break;
                    case ResultWas::FatalErrorCondition:
                        colour = Colour::Error;
                        passOrFail = "FAILED";
                        messageLabel = "due to a fatal error condition";
                        break;
                    case ResultWas::DidntThrowException:
                        colour = Colour::Error;
                        passOrFail = "FAILED";
                        messageLabel = "because no exception was thrown where one was expected";
                        break;
                    case ResultWas::Info:
                        messageLabel = "info";
                        break;
                    case ResultWas::Warning:
                        messageLabel = "warning";
                        break;
                    case ResultWas::ExplicitFailure:
                        passOrFail = "FAILED";
                        colour = Colour::Error;
                        if( _stats.infoMessages.size() == 1 )
                            messageLabel = "explicitly with message";
                        if( _stats.infoMessages.size() > 1 )
                            messageLabel = "explicitly with messages";
                        break;
                    // These cases are here to prevent compiler warnings
                    case ResultWas::Unknown:
                    case ResultWas::FailureBit:
                    case ResultWas::Exception:
                        passOrFail = "** internal error **";
                        colour = Colour::Error;
                        break;
                }
            }

            void print() const {
                printSourceInfo();
                if( stats.totals.assertions.total() > 0 ) {
                    if( result.isOk() )
                        stream << '\n';
                    printResultType();
                    printOriginalExpression();
                    printReconstructedExpression();
                }
                else {
                    stream << '\n';
                }
                printMessage();
            }

        private:
            void printResultType() const {
                if( !passOrFail.empty() ) {
                    Colour colourGuard( colour );
                    stream << passOrFail << ":\n";
                }
            }
            void printOriginalExpression() const {
                if( result.hasExpression() ) {
                    Colour colourGuard( Colour::OriginalExpression );
                    stream  << "  ";
                    stream << result.getExpressionInMacro();
                    stream << '\n';
                }
            }
            void printReconstructedExpression() const {
                if( result.hasExpandedExpression() ) {
                    stream << "with expansion:\n";
                    Colour colourGuard( Colour::ReconstructedExpression );
                    stream << Column( result.getExpandedExpression() ).indent(2) << '\n';
                }
            }
            void printMessage() const {
                if( !messageLabel.empty() )
                    stream << messageLabel << ':' << '\n';
                for( auto const& msg : messages ) {
                    // If this assertion is a warning ignore any INFO messages
                    if( printInfoMessages || msg.type != ResultWas::Info )
                        stream << Column( msg.message ).indent(2) << '\n';
                }
            }
            void printSourceInfo() const {
                Colour colourGuard( Colour::FileName );
                stream << result.getSourceInfo() << ": ";
            }

            std::ostream& stream;
            AssertionStats const& stats;
            AssertionResult const& result;
            Colour::Code colour;
            std::string passOrFail;
            std::string messageLabel;
            std::string message;
            std::vector<MessageInfo> messages;
            bool printInfoMessages;
        };

        void lazyPrint() {

            if( !currentTestRunInfo.used )
                lazyPrintRunInfo();
            if( !currentGroupInfo.used )
                lazyPrintGroupInfo();

            if( !m_headerPrinted ) {
                printTestCaseAndSectionHeader();
                m_headerPrinted = true;
            }
        }
        void lazyPrintRunInfo() {
            stream  << '\n' << getLineOfChars<'~'>() << '\n';
            Colour colour( Colour::SecondaryText );
            stream  << currentTestRunInfo->name
                    << " is a Catch v"  << libraryVersion() << " host application.\n"
                    << "Run with -? for options\n\n";

            if( m_config->rngSeed() != 0 )
                stream << "Randomness seeded to: " << m_config->rngSeed() << "\n\n";

            currentTestRunInfo.used = true;
        }
        void lazyPrintGroupInfo() {
            if( !currentGroupInfo->name.empty() && currentGroupInfo->groupsCounts > 1 ) {
                printClosedHeader( "Group: " + currentGroupInfo->name );
                currentGroupInfo.used = true;
            }
        }
        void printTestCaseAndSectionHeader() {
            assert( !m_sectionStack.empty() );
            printOpenHeader( currentTestCaseInfo->name );

            if( m_sectionStack.size() > 1 ) {
                Colour colourGuard( Colour::Headers );

                auto
                    it = m_sectionStack.begin()+1, // Skip first section (test case)
                    itEnd = m_sectionStack.end();
                for( ; it != itEnd; ++it )
                    printHeaderString( it->name, 2 );
            }

            SourceLineInfo lineInfo = m_sectionStack.back().lineInfo;

            if( !lineInfo.empty() ){
                stream << getLineOfChars<'-'>() << '\n';
                Colour colourGuard( Colour::FileName );
                stream << lineInfo << '\n';
            }
            stream << getLineOfChars<'.'>() << '\n' << std::endl;
        }

        void printClosedHeader( std::string const& _name ) {
            printOpenHeader( _name );
            stream << getLineOfChars<'.'>() << '\n';
        }
        void printOpenHeader( std::string const& _name ) {
            stream  << getLineOfChars<'-'>() << '\n';
            {
                Colour colourGuard( Colour::Headers );
                printHeaderString( _name );
            }
        }

        // if string has a : in first line will set indent to follow it on
        // subsequent lines
        void printHeaderString( std::string const& _string, std::size_t indent = 0 ) {
            std::size_t i = _string.find( ": " );
            if( i != std::string::npos )
                i+=2;
            else
                i = 0;
            stream << Column( _string ).indent( indent+i ).initialIndent( indent ) << '\n';
        }

        struct SummaryColumn {

            SummaryColumn( std::string const& _label, Colour::Code _colour )
            :   label( _label ),
                colour( _colour )
            {}
            SummaryColumn addRow( std::size_t count ) {
                std::ostringstream oss;
                oss << count;
                std::string row = oss.str();
                for( auto& oldRow : rows ) {
                    while( oldRow.size() < row.size() )
                        oldRow = ' ' + oldRow;
                    while( oldRow.size() > row.size() )
                        row = ' ' + row;
                }
                rows.push_back( row );
                return *this;
            }

            std::string label;
            Colour::Code colour;
            std::vector<std::string> rows;

        };

        void printTotals( Totals const& totals ) {
            if( totals.testCases.total() == 0 ) {
                stream << Colour( Colour::Warning ) << "No tests ran\n";
            }
            else if( totals.assertions.total() > 0 && totals.testCases.allPassed() ) {
                stream << Colour( Colour::ResultSuccess ) << "All tests passed";
                stream << " ("
                        << pluralise( totals.assertions.passed, "assertion" ) << " in "
                        << pluralise( totals.testCases.passed, "test case" ) << ')'
                        << '\n';
            }
            else {

                std::vector<SummaryColumn> columns;
                columns.push_back( SummaryColumn( "", Colour::None )
                                        .addRow( totals.testCases.total() )
                                        .addRow( totals.assertions.total() ) );
                columns.push_back( SummaryColumn( "passed", Colour::Success )
                                        .addRow( totals.testCases.passed )
                                        .addRow( totals.assertions.passed ) );
                columns.push_back( SummaryColumn( "failed", Colour::ResultError )
                                        .addRow( totals.testCases.failed )
                                        .addRow( totals.assertions.failed ) );
                columns.push_back( SummaryColumn( "failed as expected", Colour::ResultExpectedFailure )
                                        .addRow( totals.testCases.failedButOk )
                                        .addRow( totals.assertions.failedButOk ) );

                printSummaryRow( "test cases", columns, 0 );
                printSummaryRow( "assertions", columns, 1 );
            }
        }
        void printSummaryRow( std::string const& label, std::vector<SummaryColumn> const& cols, std::size_t row ) {
            for( auto col : cols ) {
                std::string value = col.rows[row];
                if( col.label.empty() ) {
                    stream << label << ": ";
                    if( value != "0" )
                        stream << value;
                    else
                        stream << Colour( Colour::Warning ) << "- none -";
                }
                else if( value != "0" ) {
                    stream  << Colour( Colour::LightGrey ) << " | ";
                    stream  << Colour( col.colour )
                            << value << ' ' << col.label;
                }
            }
            stream << '\n';
        }

        void printTotalsDivider( Totals const& totals ) {
            if( totals.testCases.total() > 0 ) {
                std::size_t failedRatio = makeRatio( totals.testCases.failed, totals.testCases.total() );
                std::size_t failedButOkRatio = makeRatio( totals.testCases.failedButOk, totals.testCases.total() );
                std::size_t passedRatio = makeRatio( totals.testCases.passed, totals.testCases.total() );
                while( failedRatio + failedButOkRatio + passedRatio < CATCH_CONFIG_CONSOLE_WIDTH-1 )
                    findMax( failedRatio, failedButOkRatio, passedRatio )++;
                while( failedRatio + failedButOkRatio + passedRatio > CATCH_CONFIG_CONSOLE_WIDTH-1 )
                    findMax( failedRatio, failedButOkRatio, passedRatio )--;

                stream << Colour( Colour::Error ) << std::string( failedRatio, '=' );
                stream << Colour( Colour::ResultExpectedFailure ) << std::string( failedButOkRatio, '=' );
                if( totals.testCases.allPassed() )
                    stream << Colour( Colour::ResultSuccess ) << std::string( passedRatio, '=' );
                else
                    stream << Colour( Colour::Success ) << std::string( passedRatio, '=' );
            }
            else {
                stream << Colour( Colour::Warning ) << std::string( CATCH_CONFIG_CONSOLE_WIDTH-1, '=' );
            }
            stream << '\n';
        }
        void printSummaryDivider() {
            stream << getLineOfChars<'-'>() << '\n';
        }

    private:
        bool m_headerPrinted = false;
    };

    INTERNAL_CATCH_REGISTER_REPORTER( "console", ConsoleReporter )

    ConsoleReporter::~ConsoleReporter() {}

} // end namespace Catch
