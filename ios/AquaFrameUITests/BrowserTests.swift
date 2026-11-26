import XCTest

final class BrowserTests: XCTestCase {
    func testHomeAndPrivateTab() {
        let app = XCUIApplication()
        app.launch()
        XCTAssertTrue(app.staticTexts["Aqua Frame"].waitForExistence(timeout: 10))
        XCTAssertTrue(app.buttons["Chess.com"].exists)
        app.buttons["Вкладки"].tap()
        app.buttons["Частная вкладка"].tap()
        XCTAssertTrue(app.staticTexts["Частная вкладка"].waitForExistence(timeout: 5))
        XCTAssertFalse(app.buttons["Закладки"].exists)
        app.buttons["Вкладки"].tap()
        app.buttons["Новая вкладка"].tap()
        XCTAssertTrue(app.buttons["Закладки"].waitForExistence(timeout: 5))
    }
}
