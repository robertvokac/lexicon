package com.robertvokac.lexicon.testing

import com.robertvokac.lexicon.AppContainer
import com.robertvokac.lexicon.auth.SessionManager
import kotlinx.coroutines.runBlocking
import org.junit.Assert.assertEquals

/** Signs in through the real login path, for tests that start behind the login screen. */
fun signInDirectly(container: AppContainer, fake: FakeLexiconServer) = runBlocking {
    assertEquals(SessionManager.LoginResult.Success, container.sessions.login(fake.baseUrl, fake.username, fake.password))
}
