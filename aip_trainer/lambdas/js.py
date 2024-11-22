js_update_ipa_output = """
function updateCssText(text, letters) {
    let wordsArr = text.split(" ")
    let lettersWordsArr = letters.split(" ")
    let speechOutputContainer = document.querySelector('#speech-output');
    speechOutputContainer.textContent = ""

    for (let idx in wordsArr) {
        let word = wordsArr[idx]
        let letterIsCorrect = lettersWordsArr[idx]
        for (let idx1 in word) {
        let letterCorrect = letterIsCorrect[idx1] == "1"
        let containerLetter = document.createElement("span")
        containerLetter.style.color = letterCorrect ? 'green' : "red"
        containerLetter.innerText = word[idx1];
        speechOutputContainer.appendChild(containerLetter)
        }
        let containerSpace = document.createElement("span")
        containerSpace.textContent = " "
        speechOutputContainer.appendChild(containerSpace)
    }
}
"""
